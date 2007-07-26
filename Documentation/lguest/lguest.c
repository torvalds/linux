/*P:100 This is the Launcher code, a simple program which lays out the
 * "physical" memory for the new Guest by mapping the kernel image and the
 * virtual devices, then reads repeatedly from /dev/lguest to run the Guest.
 *
 * The only trick: the Makefile links it at a high address so it will be clear
 * of the guest memory region.  It means that each Guest cannot have more than
 * about 2.5G of memory on a normally configured Host. :*/
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/if_tun.h>
#include <sys/uio.h>
#include <termios.h>
#include <getopt.h>
#include <zlib.h>
/*L:110 We can ignore the 28 include files we need for this program, but I do
 * want to draw attention to the use of kernel-style types.
 *
 * As Linus said, "C is a Spartan language, and so should your naming be."  I
 * like these abbreviations and the header we need uses them, so we define them
 * here.
 */
typedef unsigned long long u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#include "../../include/linux/lguest_launcher.h"
#include "../../include/asm-i386/e820.h"
/*:*/

#define PAGE_PRESENT 0x7 	/* Present, RW, Execute */
#define NET_PEERNUM 1
#define BRIDGE_PFX "bridge:"
#ifndef SIOCBRADDIF
#define SIOCBRADDIF	0x89a2		/* add interface to bridge      */
#endif

/*L:120 verbose is both a global flag and a macro.  The C preprocessor allows
 * this, and although I wouldn't recommend it, it works quite nicely here. */
static bool verbose;
#define verbose(args...) \
	do { if (verbose) printf(args); } while(0)
/*:*/

/* The pipe to send commands to the waker process */
static int waker_fd;
/* The top of guest physical memory. */
static u32 top;

/* This is our list of devices. */
struct device_list
{
	/* Summary information about the devices in our list: ready to pass to
	 * select() to ask which need servicing.*/
	fd_set infds;
	int max_infd;

	/* The descriptor page for the devices. */
	struct lguest_device_desc *descs;

	/* A single linked list of devices. */
	struct device *dev;
	/* ... And an end pointer so we can easily append new devices */
	struct device **lastdev;
};

/* The device structure describes a single device. */
struct device
{
	/* The linked-list pointer. */
	struct device *next;
	/* The descriptor for this device, as mapped into the Guest. */
	struct lguest_device_desc *desc;
	/* The memory page(s) of this device, if any.  Also mapped in Guest. */
	void *mem;

	/* If handle_input is set, it wants to be called when this file
	 * descriptor is ready. */
	int fd;
	bool (*handle_input)(int fd, struct device *me);

	/* If handle_output is set, it wants to be called when the Guest sends
	 * DMA to this key. */
	unsigned long watch_key;
	u32 (*handle_output)(int fd, const struct iovec *iov,
			     unsigned int num, struct device *me);

	/* Device-specific data. */
	void *priv;
};

/*L:130
 * Loading the Kernel.
 *
 * We start with couple of simple helper routines.  open_or_die() avoids
 * error-checking code cluttering the callers: */
static int open_or_die(const char *name, int flags)
{
	int fd = open(name, flags);
	if (fd < 0)
		err(1, "Failed to open %s", name);
	return fd;
}

/* map_zeroed_pages() takes a (page-aligned) address and a number of pages. */
static void *map_zeroed_pages(unsigned long addr, unsigned int num)
{
	/* We cache the /dev/zero file-descriptor so we only open it once. */
	static int fd = -1;

	if (fd == -1)
		fd = open_or_die("/dev/zero", O_RDONLY);

	/* We use a private mapping (ie. if we write to the page, it will be
	 * copied), and obviously we insist that it be mapped where we ask. */
	if (mmap((void *)addr, getpagesize() * num,
		 PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_PRIVATE, fd, 0)
	    != (void *)addr)
		err(1, "Mmaping %u pages of /dev/zero @%p", num, (void *)addr);

	/* Returning the address is just a courtesy: can simplify callers. */
	return (void *)addr;
}

/* To find out where to start we look for the magic Guest string, which marks
 * the code we see in lguest_asm.S.  This is a hack which we are currently
 * plotting to replace with the normal Linux entry point. */
static unsigned long entry_point(void *start, void *end,
				 unsigned long page_offset)
{
	void *p;

	/* The scan gives us the physical starting address.  We want the
	 * virtual address in this case, and fortunately, we already figured
	 * out the physical-virtual difference and passed it here in
	 * "page_offset". */
	for (p = start; p < end; p++)
		if (memcmp(p, "GenuineLguest", strlen("GenuineLguest")) == 0)
			return (long)p + strlen("GenuineLguest") + page_offset;

	err(1, "Is this image a genuine lguest?");
}

/* This routine takes an open vmlinux image, which is in ELF, and maps it into
 * the Guest memory.  ELF = Embedded Linking Format, which is the format used
 * by all modern binaries on Linux including the kernel.
 *
 * The ELF headers give *two* addresses: a physical address, and a virtual
 * address.  The Guest kernel expects to be placed in memory at the physical
 * address, and the page tables set up so it will correspond to that virtual
 * address.  We return the difference between the virtual and physical
 * addresses in the "page_offset" pointer.
 *
 * We return the starting address. */
static unsigned long map_elf(int elf_fd, const Elf32_Ehdr *ehdr,
			     unsigned long *page_offset)
{
	void *addr;
	Elf32_Phdr phdr[ehdr->e_phnum];
	unsigned int i;
	unsigned long start = -1UL, end = 0;

	/* Sanity checks on the main ELF header: an x86 executable with a
	 * reasonable number of correctly-sized program headers. */
	if (ehdr->e_type != ET_EXEC
	    || ehdr->e_machine != EM_386
	    || ehdr->e_phentsize != sizeof(Elf32_Phdr)
	    || ehdr->e_phnum < 1 || ehdr->e_phnum > 65536U/sizeof(Elf32_Phdr))
		errx(1, "Malformed elf header");

	/* An ELF executable contains an ELF header and a number of "program"
	 * headers which indicate which parts ("segments") of the program to
	 * load where. */

	/* We read in all the program headers at once: */
	if (lseek(elf_fd, ehdr->e_phoff, SEEK_SET) < 0)
		err(1, "Seeking to program headers");
	if (read(elf_fd, phdr, sizeof(phdr)) != sizeof(phdr))
		err(1, "Reading program headers");

	/* We don't know page_offset yet. */
	*page_offset = 0;

	/* Try all the headers: there are usually only three.  A read-only one,
	 * a read-write one, and a "note" section which isn't loadable. */
	for (i = 0; i < ehdr->e_phnum; i++) {
		/* If this isn't a loadable segment, we ignore it */
		if (phdr[i].p_type != PT_LOAD)
			continue;

		verbose("Section %i: size %i addr %p\n",
			i, phdr[i].p_memsz, (void *)phdr[i].p_paddr);

		/* We expect a simple linear address space: every segment must
		 * have the same difference between virtual (p_vaddr) and
		 * physical (p_paddr) address. */
		if (!*page_offset)
			*page_offset = phdr[i].p_vaddr - phdr[i].p_paddr;
		else if (*page_offset != phdr[i].p_vaddr - phdr[i].p_paddr)
			errx(1, "Page offset of section %i different", i);

		/* We track the first and last address we mapped, so we can
		 * tell entry_point() where to scan. */
		if (phdr[i].p_paddr < start)
			start = phdr[i].p_paddr;
		if (phdr[i].p_paddr + phdr[i].p_filesz > end)
			end = phdr[i].p_paddr + phdr[i].p_filesz;

		/* We map this section of the file at its physical address.  We
		 * map it read & write even if the header says this segment is
		 * read-only.  The kernel really wants to be writable: it
		 * patches its own instructions which would normally be
		 * read-only.
		 *
		 * MAP_PRIVATE means that the page won't be copied until a
		 * write is done to it.  This allows us to share much of the
		 * kernel memory between Guests. */
		addr = mmap((void *)phdr[i].p_paddr,
			    phdr[i].p_filesz,
			    PROT_READ|PROT_WRITE|PROT_EXEC,
			    MAP_FIXED|MAP_PRIVATE,
			    elf_fd, phdr[i].p_offset);
		if (addr != (void *)phdr[i].p_paddr)
			err(1, "Mmaping vmlinux seg %i gave %p not %p",
			    i, addr, (void *)phdr[i].p_paddr);
	}

	return entry_point((void *)start, (void *)end, *page_offset);
}

/*L:170 Prepare to be SHOCKED and AMAZED.  And possibly a trifle nauseated.
 *
 * We know that CONFIG_PAGE_OFFSET sets what virtual address the kernel expects
 * to be.  We don't know what that option was, but we can figure it out
 * approximately by looking at the addresses in the code.  I chose the common
 * case of reading a memory location into the %eax register:
 *
 *  movl <some-address>, %eax
 *
 * This gets encoded as five bytes: "0xA1 <4-byte-address>".  For example,
 * "0xA1 0x18 0x60 0x47 0xC0" reads the address 0xC0476018 into %eax.
 *
 * In this example can guess that the kernel was compiled with
 * CONFIG_PAGE_OFFSET set to 0xC0000000 (it's always a round number).  If the
 * kernel were larger than 16MB, we might see 0xC1 addresses show up, but our
 * kernel isn't that bloated yet.
 *
 * Unfortunately, x86 has variable-length instructions, so finding this
 * particular instruction properly involves writing a disassembler.  Instead,
 * we rely on statistics.  We look for "0xA1" and tally the different bytes
 * which occur 4 bytes later (the "0xC0" in our example above).  When one of
 * those bytes appears three times, we can be reasonably confident that it
 * forms the start of CONFIG_PAGE_OFFSET.
 *
 * This is amazingly reliable. */
static unsigned long intuit_page_offset(unsigned char *img, unsigned long len)
{
	unsigned int i, possibilities[256] = { 0 };

	for (i = 0; i + 4 < len; i++) {
		/* mov 0xXXXXXXXX,%eax */
		if (img[i] == 0xA1 && ++possibilities[img[i+4]] > 3)
			return (unsigned long)img[i+4] << 24;
	}
	errx(1, "could not determine page offset");
}

/*L:160 Unfortunately the entire ELF image isn't compressed: the segments
 * which need loading are extracted and compressed raw.  This denies us the
 * information we need to make a fully-general loader. */
static unsigned long unpack_bzimage(int fd, unsigned long *page_offset)
{
	gzFile f;
	int ret, len = 0;
	/* A bzImage always gets loaded at physical address 1M.  This is
	 * actually configurable as CONFIG_PHYSICAL_START, but as the comment
	 * there says, "Don't change this unless you know what you are doing".
	 * Indeed. */
	void *img = (void *)0x100000;

	/* gzdopen takes our file descriptor (carefully placed at the start of
	 * the GZIP header we found) and returns a gzFile. */
	f = gzdopen(fd, "rb");
	/* We read it into memory in 64k chunks until we hit the end. */
	while ((ret = gzread(f, img + len, 65536)) > 0)
		len += ret;
	if (ret < 0)
		err(1, "reading image from bzImage");

	verbose("Unpacked size %i addr %p\n", len, img);

	/* Without the ELF header, we can't tell virtual-physical gap.  This is
	 * CONFIG_PAGE_OFFSET, and people do actually change it.  Fortunately,
	 * I have a clever way of figuring it out from the code itself.  */
	*page_offset = intuit_page_offset(img, len);

	return entry_point(img, img + len, *page_offset);
}

/*L:150 A bzImage, unlike an ELF file, is not meant to be loaded.  You're
 * supposed to jump into it and it will unpack itself.  We can't do that
 * because the Guest can't run the unpacking code, and adding features to
 * lguest kills puppies, so we don't want to.
 *
 * The bzImage is formed by putting the decompressing code in front of the
 * compressed kernel code.  So we can simple scan through it looking for the
 * first "gzip" header, and start decompressing from there. */
static unsigned long load_bzimage(int fd, unsigned long *page_offset)
{
	unsigned char c;
	int state = 0;

	/* GZIP header is 0x1F 0x8B <method> <flags>... <compressed-by>. */
	while (read(fd, &c, 1) == 1) {
		switch (state) {
		case 0:
			if (c == 0x1F)
				state++;
			break;
		case 1:
			if (c == 0x8B)
				state++;
			else
				state = 0;
			break;
		case 2 ... 8:
			state++;
			break;
		case 9:
			/* Seek back to the start of the gzip header. */
			lseek(fd, -10, SEEK_CUR);
			/* One final check: "compressed under UNIX". */
			if (c != 0x03)
				state = -1;
			else
				return unpack_bzimage(fd, page_offset);
		}
	}
	errx(1, "Could not find kernel in bzImage");
}

/*L:140 Loading the kernel is easy when it's a "vmlinux", but most kernels
 * come wrapped up in the self-decompressing "bzImage" format.  With some funky
 * coding, we can load those, too. */
static unsigned long load_kernel(int fd, unsigned long *page_offset)
{
	Elf32_Ehdr hdr;

	/* Read in the first few bytes. */
	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		err(1, "Reading kernel");

	/* If it's an ELF file, it starts with "\177ELF" */
	if (memcmp(hdr.e_ident, ELFMAG, SELFMAG) == 0)
		return map_elf(fd, &hdr, page_offset);

	/* Otherwise we assume it's a bzImage, and try to unpack it */
	return load_bzimage(fd, page_offset);
}

/* This is a trivial little helper to align pages.  Andi Kleen hated it because
 * it calls getpagesize() twice: "it's dumb code."
 *
 * Kernel guys get really het up about optimization, even when it's not
 * necessary.  I leave this code as a reaction against that. */
static inline unsigned long page_align(unsigned long addr)
{
	/* Add upwards and truncate downwards. */
	return ((addr + getpagesize()-1) & ~(getpagesize()-1));
}

/*L:180 An "initial ram disk" is a disk image loaded into memory along with
 * the kernel which the kernel can use to boot from without needing any
 * drivers.  Most distributions now use this as standard: the initrd contains
 * the code to load the appropriate driver modules for the current machine.
 *
 * Importantly, James Morris works for RedHat, and Fedora uses initrds for its
 * kernels.  He sent me this (and tells me when I break it). */
static unsigned long load_initrd(const char *name, unsigned long mem)
{
	int ifd;
	struct stat st;
	unsigned long len;
	void *iaddr;

	ifd = open_or_die(name, O_RDONLY);
	/* fstat() is needed to get the file size. */
	if (fstat(ifd, &st) < 0)
		err(1, "fstat() on initrd '%s'", name);

	/* The length needs to be rounded up to a page size: mmap needs the
	 * address to be page aligned. */
	len = page_align(st.st_size);
	/* We map the initrd at the top of memory. */
	iaddr = mmap((void *)mem - len, st.st_size,
		     PROT_READ|PROT_EXEC|PROT_WRITE,
		     MAP_FIXED|MAP_PRIVATE, ifd, 0);
	if (iaddr != (void *)mem - len)
		err(1, "Mmaping initrd '%s' returned %p not %p",
		    name, iaddr, (void *)mem - len);
	/* Once a file is mapped, you can close the file descriptor.  It's a
	 * little odd, but quite useful. */
	close(ifd);
	verbose("mapped initrd %s size=%lu @ %p\n", name, st.st_size, iaddr);

	/* We return the initrd size. */
	return len;
}

/* Once we know how much memory we have, and the address the Guest kernel
 * expects, we can construct simple linear page tables which will get the Guest
 * far enough into the boot to create its own.
 *
 * We lay them out of the way, just below the initrd (which is why we need to
 * know its size). */
static unsigned long setup_pagetables(unsigned long mem,
				      unsigned long initrd_size,
				      unsigned long page_offset)
{
	u32 *pgdir, *linear;
	unsigned int mapped_pages, i, linear_pages;
	unsigned int ptes_per_page = getpagesize()/sizeof(u32);

	/* Ideally we map all physical memory starting at page_offset.
	 * However, if page_offset is 0xC0000000 we can only map 1G of physical
	 * (0xC0000000 + 1G overflows). */
	if (mem <= -page_offset)
		mapped_pages = mem/getpagesize();
	else
		mapped_pages = -page_offset/getpagesize();

	/* Each PTE page can map ptes_per_page pages: how many do we need? */
	linear_pages = (mapped_pages + ptes_per_page-1)/ptes_per_page;

	/* We put the toplevel page directory page at the top of memory. */
	pgdir = (void *)mem - initrd_size - getpagesize();

	/* Now we use the next linear_pages pages as pte pages */
	linear = (void *)pgdir - linear_pages*getpagesize();

	/* Linear mapping is easy: put every page's address into the mapping in
	 * order.  PAGE_PRESENT contains the flags Present, Writable and
	 * Executable. */
	for (i = 0; i < mapped_pages; i++)
		linear[i] = ((i * getpagesize()) | PAGE_PRESENT);

	/* The top level points to the linear page table pages above.  The
	 * entry representing page_offset points to the first one, and they
	 * continue from there. */
	for (i = 0; i < mapped_pages; i += ptes_per_page) {
		pgdir[(i + page_offset/getpagesize())/ptes_per_page]
			= (((u32)linear + i*sizeof(u32)) | PAGE_PRESENT);
	}

	verbose("Linear mapping of %u pages in %u pte pages at %p\n",
		mapped_pages, linear_pages, linear);

	/* We return the top level (guest-physical) address: the kernel needs
	 * to know where it is. */
	return (unsigned long)pgdir;
}

/* Simple routine to roll all the commandline arguments together with spaces
 * between them. */
static void concat(char *dst, char *args[])
{
	unsigned int i, len = 0;

	for (i = 0; args[i]; i++) {
		strcpy(dst+len, args[i]);
		strcat(dst+len, " ");
		len += strlen(args[i]) + 1;
	}
	/* In case it's empty. */
	dst[len] = '\0';
}

/* This is where we actually tell the kernel to initialize the Guest.  We saw
 * the arguments it expects when we looked at initialize() in lguest_user.c:
 * the top physical page to allow, the top level pagetable, the entry point and
 * the page_offset constant for the Guest. */
static int tell_kernel(u32 pgdir, u32 start, u32 page_offset)
{
	u32 args[] = { LHREQ_INITIALIZE,
		       top/getpagesize(), pgdir, start, page_offset };
	int fd;

	fd = open_or_die("/dev/lguest", O_RDWR);
	if (write(fd, args, sizeof(args)) < 0)
		err(1, "Writing to /dev/lguest");

	/* We return the /dev/lguest file descriptor to control this Guest */
	return fd;
}
/*:*/

static void set_fd(int fd, struct device_list *devices)
{
	FD_SET(fd, &devices->infds);
	if (fd > devices->max_infd)
		devices->max_infd = fd;
}

/*L:200
 * The Waker.
 *
 * With a console and network devices, we can have lots of input which we need
 * to process.  We could try to tell the kernel what file descriptors to watch,
 * but handing a file descriptor mask through to the kernel is fairly icky.
 *
 * Instead, we fork off a process which watches the file descriptors and writes
 * the LHREQ_BREAK command to the /dev/lguest filedescriptor to tell the Host
 * loop to stop running the Guest.  This causes it to return from the
 * /dev/lguest read with -EAGAIN, where it will write to /dev/lguest to reset
 * the LHREQ_BREAK and wake us up again.
 *
 * This, of course, is merely a different *kind* of icky.
 */
static void wake_parent(int pipefd, int lguest_fd, struct device_list *devices)
{
	/* Add the pipe from the Launcher to the fdset in the device_list, so
	 * we watch it, too. */
	set_fd(pipefd, devices);

	for (;;) {
		fd_set rfds = devices->infds;
		u32 args[] = { LHREQ_BREAK, 1 };

		/* Wait until input is ready from one of the devices. */
		select(devices->max_infd+1, &rfds, NULL, NULL, NULL);
		/* Is it a message from the Launcher? */
		if (FD_ISSET(pipefd, &rfds)) {
			int ignorefd;
			/* If read() returns 0, it means the Launcher has
			 * exited.  We silently follow. */
			if (read(pipefd, &ignorefd, sizeof(ignorefd)) == 0)
				exit(0);
			/* Otherwise it's telling us there's a problem with one
			 * of the devices, and we should ignore that file
			 * descriptor from now on. */
			FD_CLR(ignorefd, &devices->infds);
		} else /* Send LHREQ_BREAK command. */
			write(lguest_fd, args, sizeof(args));
	}
}

/* This routine just sets up a pipe to the Waker process. */
static int setup_waker(int lguest_fd, struct device_list *device_list)
{
	int pipefd[2], child;

	/* We create a pipe to talk to the waker, and also so it knows when the
	 * Launcher dies (and closes pipe). */
	pipe(pipefd);
	child = fork();
	if (child == -1)
		err(1, "forking");

	if (child == 0) {
		/* Close the "writing" end of our copy of the pipe */
		close(pipefd[1]);
		wake_parent(pipefd[0], lguest_fd, device_list);
	}
	/* Close the reading end of our copy of the pipe. */
	close(pipefd[0]);

	/* Here is the fd used to talk to the waker. */
	return pipefd[1];
}

/*L:210
 * Device Handling.
 *
 * When the Guest sends DMA to us, it sends us an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which check it and exits with an error message
 * if something funny is going on:
 */
static void *_check_pointer(unsigned long addr, unsigned int size,
			    unsigned int line)
{
	/* We have to separately check addr and addr+size, because size could
	 * be huge and addr + size might wrap around. */
	if (addr >= top || addr + size >= top)
		errx(1, "%s:%i: Invalid address %li", __FILE__, line, addr);
	/* We return a pointer for the caller's convenience, now we know it's
	 * safe to use. */
	return (void *)addr;
}
/* A macro which transparently hands the line number to the real function. */
#define check_pointer(addr,size) _check_pointer(addr, size, __LINE__)

/* The Guest has given us the address of a "struct lguest_dma".  We check it's
 * OK and convert it to an iovec (which is a simple array of ptr/size
 * pairs). */
static u32 *dma2iov(unsigned long dma, struct iovec iov[], unsigned *num)
{
	unsigned int i;
	struct lguest_dma *udma;

	/* First we make sure that the array memory itself is valid. */
	udma = check_pointer(dma, sizeof(*udma));
	/* Now we check each element */
	for (i = 0; i < LGUEST_MAX_DMA_SECTIONS; i++) {
		/* A zero length ends the array. */
		if (!udma->len[i])
			break;

		iov[i].iov_base = check_pointer(udma->addr[i], udma->len[i]);
		iov[i].iov_len = udma->len[i];
	}
	*num = i;

	/* We return the pointer to where the caller should write the amount of
	 * the buffer used. */
	return &udma->used_len;
}

/* This routine gets a DMA buffer from the Guest for a given key, and converts
 * it to an iovec array.  It returns the interrupt the Guest wants when we're
 * finished, and a pointer to the "used_len" field to fill in. */
static u32 *get_dma_buffer(int fd, void *key,
			   struct iovec iov[], unsigned int *num, u32 *irq)
{
	u32 buf[] = { LHREQ_GETDMA, (u32)key };
	unsigned long udma;
	u32 *res;

	/* Ask the kernel for a DMA buffer corresponding to this key. */
	udma = write(fd, buf, sizeof(buf));
	/* They haven't registered any, or they're all used? */
	if (udma == (unsigned long)-1)
		return NULL;

	/* Convert it into our iovec array */
	res = dma2iov(udma, iov, num);
	/* The kernel stashes irq in ->used_len to get it out to us. */
	*irq = *res;
	/* Return a pointer to ((struct lguest_dma *)udma)->used_len. */
	return res;
}

/* This is a convenient routine to send the Guest an interrupt. */
static void trigger_irq(int fd, u32 irq)
{
	u32 buf[] = { LHREQ_IRQ, irq };
	if (write(fd, buf, sizeof(buf)) != 0)
		err(1, "Triggering irq %i", irq);
}

/* This simply sets up an iovec array where we can put data to be discarded.
 * This happens when the Guest doesn't want or can't handle the input: we have
 * to get rid of it somewhere, and if we bury it in the ceiling space it will
 * start to smell after a week. */
static void discard_iovec(struct iovec *iov, unsigned int *num)
{
	static char discard_buf[1024];
	*num = 1;
	iov->iov_base = discard_buf;
	iov->iov_len = sizeof(discard_buf);
}

/* Here is the input terminal setting we save, and the routine to restore them
 * on exit so the user can see what they type next. */
static struct termios orig_term;
static void restore_term(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
}

/* We associate some data with the console for our exit hack. */
struct console_abort
{
	/* How many times have they hit ^C? */
	int count;
	/* When did they start? */
	struct timeval start;
};

/* This is the routine which handles console input (ie. stdin). */
static bool handle_console_input(int fd, struct device *dev)
{
	u32 irq = 0, *lenp;
	int len;
	unsigned int num;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];
	struct console_abort *abort = dev->priv;

	/* First we get the console buffer from the Guest.  The key is dev->mem
	 * which was set to 0 in setup_console(). */
	lenp = get_dma_buffer(fd, dev->mem, iov, &num, &irq);
	if (!lenp) {
		/* If it's not ready for input, warn and set up to discard. */
		warn("console: no dma buffer!");
		discard_iovec(iov, &num);
	}

	/* This is why we convert to iovecs: the readv() call uses them, and so
	 * it reads straight into the Guest's buffer. */
	len = readv(dev->fd, iov, num);
	if (len <= 0) {
		/* This implies that the console is closed, is /dev/null, or
		 * something went terribly wrong.  We still go through the rest
		 * of the logic, though, especially the exit handling below. */
		warnx("Failed to get console input, ignoring console.");
		len = 0;
	}

	/* If we read the data into the Guest, fill in the length and send the
	 * interrupt. */
	if (lenp) {
		*lenp = len;
		trigger_irq(fd, irq);
	}

	/* Three ^C within one second?  Exit.
	 *
	 * This is such a hack, but works surprisingly well.  Each ^C has to be
	 * in a buffer by itself, so they can't be too fast.  But we check that
	 * we get three within about a second, so they can't be too slow. */
	if (len == 1 && ((char *)iov[0].iov_base)[0] == 3) {
		if (!abort->count++)
			gettimeofday(&abort->start, NULL);
		else if (abort->count == 3) {
			struct timeval now;
			gettimeofday(&now, NULL);
			if (now.tv_sec <= abort->start.tv_sec+1) {
				u32 args[] = { LHREQ_BREAK, 0 };
				/* Close the fd so Waker will know it has to
				 * exit. */
				close(waker_fd);
				/* Just in case waker is blocked in BREAK, send
				 * unbreak now. */
				write(fd, args, sizeof(args));
				exit(2);
			}
			abort->count = 0;
		}
	} else
		/* Any other key resets the abort counter. */
		abort->count = 0;

	/* Now, if we didn't read anything, put the input terminal back and
	 * return failure (meaning, don't call us again). */
	if (!len) {
		restore_term();
		return false;
	}
	/* Everything went OK! */
	return true;
}

/* Handling console output is much simpler than input. */
static u32 handle_console_output(int fd, const struct iovec *iov,
				 unsigned num, struct device*dev)
{
	/* Whatever the Guest sends, write it to standard output.  Return the
	 * number of bytes written. */
	return writev(STDOUT_FILENO, iov, num);
}

/* Guest->Host network output is also pretty easy. */
static u32 handle_tun_output(int fd, const struct iovec *iov,
			     unsigned num, struct device *dev)
{
	/* We put a flag in the "priv" pointer of the network device, and set
	 * it as soon as we see output.  We'll see why in handle_tun_input() */
	*(bool *)dev->priv = true;
	/* Whatever packet the Guest sent us, write it out to the tun
	 * device. */
	return writev(dev->fd, iov, num);
}

/* This matches the peer_key() in lguest_net.c.  The key for any given slot
 * is the address of the network device's page plus 4 * the slot number. */
static unsigned long peer_offset(unsigned int peernum)
{
	return 4 * peernum;
}

/* This is where we handle a packet coming in from the tun device */
static bool handle_tun_input(int fd, struct device *dev)
{
	u32 irq = 0, *lenp;
	int len;
	unsigned num;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];

	/* First we get a buffer the Guest has bound to its key. */
	lenp = get_dma_buffer(fd, dev->mem+peer_offset(NET_PEERNUM), iov, &num,
			      &irq);
	if (!lenp) {
		/* Now, it's expected that if we try to send a packet too
		 * early, the Guest won't be ready yet.  This is why we set a
		 * flag when the Guest sends its first packet.  If it's sent a
		 * packet we assume it should be ready to receive them.
		 *
		 * Actually, this is what the status bits in the descriptor are
		 * for: we should *use* them.  FIXME! */
		if (*(bool *)dev->priv)
			warn("network: no dma buffer!");
		discard_iovec(iov, &num);
	}

	/* Read the packet from the device directly into the Guest's buffer. */
	len = readv(dev->fd, iov, num);
	if (len <= 0)
		err(1, "reading network");

	/* Write the used_len, and trigger the interrupt for the Guest */
	if (lenp) {
		*lenp = len;
		trigger_irq(fd, irq);
	}
	verbose("tun input packet len %i [%02x %02x] (%s)\n", len,
		((u8 *)iov[0].iov_base)[0], ((u8 *)iov[0].iov_base)[1],
		lenp ? "sent" : "discarded");
	/* All good. */
	return true;
}

/* The last device handling routine is block output: the Guest has sent a DMA
 * to the block device.  It will have placed the command it wants in the
 * "struct lguest_block_page". */
static u32 handle_block_output(int fd, const struct iovec *iov,
			       unsigned num, struct device *dev)
{
	struct lguest_block_page *p = dev->mem;
	u32 irq, *lenp;
	unsigned int len, reply_num;
	struct iovec reply[LGUEST_MAX_DMA_SECTIONS];
	off64_t device_len, off = (off64_t)p->sector * 512;

	/* First we extract the device length from the dev->priv pointer. */
	device_len = *(off64_t *)dev->priv;

	/* We first check that the read or write is within the length of the
	 * block file. */
	if (off >= device_len)
		err(1, "Bad offset %llu vs %llu", off, device_len);
	/* Move to the right location in the block file.  This shouldn't fail,
	 * but best to check. */
	if (lseek64(dev->fd, off, SEEK_SET) != off)
		err(1, "Bad seek to sector %i", p->sector);

	verbose("Block: %s at offset %llu\n", p->type ? "WRITE" : "READ", off);

	/* They were supposed to bind a reply buffer at key equal to the start
	 * of the block device memory.  We need this to tell them when the
	 * request is finished. */
	lenp = get_dma_buffer(fd, dev->mem, reply, &reply_num, &irq);
	if (!lenp)
		err(1, "Block request didn't give us a dma buffer");

	if (p->type) {
		/* A write request.  The DMA they sent contained the data, so
		 * write it out. */
		len = writev(dev->fd, iov, num);
		/* Grr... Now we know how long the "struct lguest_dma" they
		 * sent was, we make sure they didn't try to write over the end
		 * of the block file (possibly extending it). */
		if (off + len > device_len) {
			/* Trim it back to the correct length */
			ftruncate(dev->fd, device_len);
			/* Die, bad Guest, die. */
			errx(1, "Write past end %llu+%u", off, len);
		}
		/* The reply length is 0: we just send back an empty DMA to
		 * interrupt them and tell them the write is finished. */
		*lenp = 0;
	} else {
		/* A read request.  They sent an empty DMA to start the
		 * request, and we put the read contents into the reply
		 * buffer. */
		len = readv(dev->fd, reply, reply_num);
		*lenp = len;
	}

	/* The result is 1 (done), 2 if there was an error (short read or
	 * write). */
	p->result = 1 + (p->bytes != len);
	/* Now tell them we've used their reply buffer. */
	trigger_irq(fd, irq);

	/* We're supposed to return the number of bytes of the output buffer we
	 * used.  But the block device uses the "result" field instead, so we
	 * don't bother. */
	return 0;
}

/* This is the generic routine we call when the Guest sends some DMA out. */
static void handle_output(int fd, unsigned long dma, unsigned long key,
			  struct device_list *devices)
{
	struct device *i;
	u32 *lenp;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];
	unsigned num = 0;

	/* Convert the "struct lguest_dma" they're sending to a "struct
	 * iovec". */
	lenp = dma2iov(dma, iov, &num);

	/* Check each device: if they expect output to this key, tell them to
	 * handle it. */
	for (i = devices->dev; i; i = i->next) {
		if (i->handle_output && key == i->watch_key) {
			/* We write the result straight into the used_len field
			 * for them. */
			*lenp = i->handle_output(fd, iov, num, i);
			return;
		}
	}

	/* This can happen: the kernel sends any SEND_DMA which doesn't match
	 * another Guest to us.  It could be that another Guest just left a
	 * network, for example.  But it's unusual. */
	warnx("Pending dma %p, key %p", (void *)dma, (void *)key);
}

/* This is called when the waker wakes us up: check for incoming file
 * descriptors. */
static void handle_input(int fd, struct device_list *devices)
{
	/* select() wants a zeroed timeval to mean "don't wait". */
	struct timeval poll = { .tv_sec = 0, .tv_usec = 0 };

	for (;;) {
		struct device *i;
		fd_set fds = devices->infds;

		/* If nothing is ready, we're done. */
		if (select(devices->max_infd+1, &fds, NULL, NULL, &poll) == 0)
			break;

		/* Otherwise, call the device(s) which have readable
		 * file descriptors and a method of handling them.  */
		for (i = devices->dev; i; i = i->next) {
			if (i->handle_input && FD_ISSET(i->fd, &fds)) {
				/* If handle_input() returns false, it means we
				 * should no longer service it.
				 * handle_console_input() does this. */
				if (!i->handle_input(fd, i)) {
					/* Clear it from the set of input file
					 * descriptors kept at the head of the
					 * device list. */
					FD_CLR(i->fd, &devices->infds);
					/* Tell waker to ignore it too... */
					write(waker_fd, &i->fd, sizeof(i->fd));
				}
			}
		}
	}
}

/*L:190
 * Device Setup
 *
 * All devices need a descriptor so the Guest knows it exists, and a "struct
 * device" so the Launcher can keep track of it.  We have common helper
 * routines to allocate them.
 *
 * This routine allocates a new "struct lguest_device_desc" from descriptor
 * table in the devices array just above the Guest's normal memory. */
static struct lguest_device_desc *
new_dev_desc(struct lguest_device_desc *descs,
	     u16 type, u16 features, u16 num_pages)
{
	unsigned int i;

	for (i = 0; i < LGUEST_MAX_DEVICES; i++) {
		if (!descs[i].type) {
			descs[i].type = type;
			descs[i].features = features;
			descs[i].num_pages = num_pages;
			/* If they said the device needs memory, we allocate
			 * that now, bumping up the top of Guest memory. */
			if (num_pages) {
				map_zeroed_pages(top, num_pages);
				descs[i].pfn = top/getpagesize();
				top += num_pages*getpagesize();
			}
			return &descs[i];
		}
	}
	errx(1, "too many devices");
}

/* This monster routine does all the creation and setup of a new device,
 * including caling new_dev_desc() to allocate the descriptor and device
 * memory. */
static struct device *new_device(struct device_list *devices,
				 u16 type, u16 num_pages, u16 features,
				 int fd,
				 bool (*handle_input)(int, struct device *),
				 unsigned long watch_off,
				 u32 (*handle_output)(int,
						      const struct iovec *,
						      unsigned,
						      struct device *))
{
	struct device *dev = malloc(sizeof(*dev));

	/* Append to device list.  Prepending to a single-linked list is
	 * easier, but the user expects the devices to be arranged on the bus
	 * in command-line order.  The first network device on the command line
	 * is eth0, the first block device /dev/lgba, etc. */
	*devices->lastdev = dev;
	dev->next = NULL;
	devices->lastdev = &dev->next;

	/* Now we populate the fields one at a time. */
	dev->fd = fd;
	/* If we have an input handler for this file descriptor, then we add it
	 * to the device_list's fdset and maxfd. */
	if (handle_input)
		set_fd(dev->fd, devices);
	dev->desc = new_dev_desc(devices->descs, type, features, num_pages);
	dev->mem = (void *)(dev->desc->pfn * getpagesize());
	dev->handle_input = handle_input;
	dev->watch_key = (unsigned long)dev->mem + watch_off;
	dev->handle_output = handle_output;
	return dev;
}

/* Our first setup routine is the console.  It's a fairly simple device, but
 * UNIX tty handling makes it uglier than it could be. */
static void setup_console(struct device_list *devices)
{
	struct device *dev;

	/* If we can save the initial standard input settings... */
	if (tcgetattr(STDIN_FILENO, &orig_term) == 0) {
		struct termios term = orig_term;
		/* Then we turn off echo, line buffering and ^C etc.  We want a
		 * raw input stream to the Guest. */
		term.c_lflag &= ~(ISIG|ICANON|ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
		/* If we exit gracefully, the original settings will be
		 * restored so the user can see what they're typing. */
		atexit(restore_term);
	}

	/* We don't currently require any memory for the console, so we ask for
	 * 0 pages. */
	dev = new_device(devices, LGUEST_DEVICE_T_CONSOLE, 0, 0,
			 STDIN_FILENO, handle_console_input,
			 LGUEST_CONSOLE_DMA_KEY, handle_console_output);
	/* We store the console state in dev->priv, and initialize it. */
	dev->priv = malloc(sizeof(struct console_abort));
	((struct console_abort *)dev->priv)->count = 0;
	verbose("device %p: console\n",
		(void *)(dev->desc->pfn * getpagesize()));
}

/* Setting up a block file is also fairly straightforward. */
static void setup_block_file(const char *filename, struct device_list *devices)
{
	int fd;
	struct device *dev;
	off64_t *device_len;
	struct lguest_block_page *p;

	/* We open with O_LARGEFILE because otherwise we get stuck at 2G.  We
	 * open with O_DIRECT because otherwise our benchmarks go much too
	 * fast. */
	fd = open_or_die(filename, O_RDWR|O_LARGEFILE|O_DIRECT);

	/* We want one page, and have no input handler (the block file never
	 * has anything interesting to say to us).  Our timing will be quite
	 * random, so it should be a reasonable randomness source. */
	dev = new_device(devices, LGUEST_DEVICE_T_BLOCK, 1,
			 LGUEST_DEVICE_F_RANDOMNESS,
			 fd, NULL, 0, handle_block_output);

	/* We store the device size in the private area */
	device_len = dev->priv = malloc(sizeof(*device_len));
	/* This is the safe way of establishing the size of our device: it
	 * might be a normal file or an actual block device like /dev/hdb. */
	*device_len = lseek64(fd, 0, SEEK_END);

	/* The device memory is a "struct lguest_block_page".  It's zeroed
	 * already, we just need to put in the device size.  Block devices
	 * think in sectors (ie. 512 byte chunks), so we translate here. */
	p = dev->mem;
	p->num_sectors = *device_len/512;
	verbose("device %p: block %i sectors\n",
		(void *)(dev->desc->pfn * getpagesize()), p->num_sectors);
}

/*
 * Network Devices.
 *
 * Setting up network devices is quite a pain, because we have three types.
 * First, we have the inter-Guest network.  This is a file which is mapped into
 * the address space of the Guests who are on the network.  Because it is a
 * shared mapping, the same page underlies all the devices, and they can send
 * DMA to each other.
 *
 * Remember from our network driver, the Guest is told what slot in the page it
 * is to use.  We use exclusive fnctl locks to reserve a slot.  If another
 * Guest is using a slot, the lock will fail and we try another.  Because fnctl
 * locks are cleaned up automatically when we die, this cleverly means that our
 * reservation on the slot will vanish if we crash. */
static unsigned int find_slot(int netfd, const char *filename)
{
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_len = 1;
	/* Try a 1 byte lock in each possible position number */
	for (fl.l_start = 0;
	     fl.l_start < getpagesize()/sizeof(struct lguest_net);
	     fl.l_start++) {
		/* If we succeed, return the slot number. */
		if (fcntl(netfd, F_SETLK, &fl) == 0)
			return fl.l_start;
	}
	errx(1, "No free slots in network file %s", filename);
}

/* This function sets up the network file */
static void setup_net_file(const char *filename,
			   struct device_list *devices)
{
	int netfd;
	struct device *dev;

	/* We don't use open_or_die() here: for friendliness we create the file
	 * if it doesn't already exist. */
	netfd = open(filename, O_RDWR, 0);
	if (netfd < 0) {
		if (errno == ENOENT) {
			netfd = open(filename, O_RDWR|O_CREAT, 0600);
			if (netfd >= 0) {
				/* If we succeeded, initialize the file with a
				 * blank page. */
				char page[getpagesize()];
				memset(page, 0, sizeof(page));
				write(netfd, page, sizeof(page));
			}
		}
		if (netfd < 0)
			err(1, "cannot open net file '%s'", filename);
	}

	/* We need 1 page, and the features indicate the slot to use and that
	 * no checksum is needed.  We never touch this device again; it's
	 * between the Guests on the network, so we don't register input or
	 * output handlers. */
	dev = new_device(devices, LGUEST_DEVICE_T_NET, 1,
			 find_slot(netfd, filename)|LGUEST_NET_F_NOCSUM,
			 -1, NULL, 0, NULL);

	/* Map the shared file. */
	if (mmap(dev->mem, getpagesize(), PROT_READ|PROT_WRITE,
			 MAP_FIXED|MAP_SHARED, netfd, 0) != dev->mem)
			err(1, "could not mmap '%s'", filename);
	verbose("device %p: shared net %s, peer %i\n",
		(void *)(dev->desc->pfn * getpagesize()), filename,
		dev->desc->features & ~LGUEST_NET_F_NOCSUM);
}
/*:*/

static u32 str2ip(const char *ipaddr)
{
	unsigned int byte[4];

	sscanf(ipaddr, "%u.%u.%u.%u", &byte[0], &byte[1], &byte[2], &byte[3]);
	return (byte[0] << 24) | (byte[1] << 16) | (byte[2] << 8) | byte[3];
}

/* This code is "adapted" from libbridge: it attaches the Host end of the
 * network device to the bridge device specified by the command line.
 *
 * This is yet another James Morris contribution (I'm an IP-level guy, so I
 * dislike bridging), and I just try not to break it. */
static void add_to_bridge(int fd, const char *if_name, const char *br_name)
{
	int ifidx;
	struct ifreq ifr;

	if (!*br_name)
		errx(1, "must specify bridge name");

	ifidx = if_nametoindex(if_name);
	if (!ifidx)
		errx(1, "interface %s does not exist!", if_name);

	strncpy(ifr.ifr_name, br_name, IFNAMSIZ);
	ifr.ifr_ifindex = ifidx;
	if (ioctl(fd, SIOCBRADDIF, &ifr) < 0)
		err(1, "can't add %s to bridge %s", if_name, br_name);
}

/* This sets up the Host end of the network device with an IP address, brings
 * it up so packets will flow, the copies the MAC address into the hwaddr
 * pointer (in practice, the Host's slot in the network device's memory). */
static void configure_device(int fd, const char *devname, u32 ipaddr,
			     unsigned char hwaddr[6])
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

	/* Don't read these incantations.  Just cut & paste them like I did! */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, devname);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(ipaddr);
	if (ioctl(fd, SIOCSIFADDR, &ifr) != 0)
		err(1, "Setting %s interface address", devname);
	ifr.ifr_flags = IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		err(1, "Bringing interface %s up", devname);

	/* SIOC stands for Socket I/O Control.  G means Get (vs S for Set
	 * above).  IF means Interface, and HWADDR is hardware address.
	 * Simple! */
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0)
		err(1, "getting hw address for %s", devname);
	memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);
}

/*L:195 The other kind of network is a Host<->Guest network.  This can either
 * use briding or routing, but the principle is the same: it uses the "tun"
 * device to inject packets into the Host as if they came in from a normal
 * network card.  We just shunt packets between the Guest and the tun
 * device. */
static void setup_tun_net(const char *arg, struct device_list *devices)
{
	struct device *dev;
	struct ifreq ifr;
	int netfd, ipfd;
	u32 ip;
	const char *br_name = NULL;

	/* We open the /dev/net/tun device and tell it we want a tap device.  A
	 * tap device is like a tun device, only somehow different.  To tell
	 * the truth, I completely blundered my way through this code, but it
	 * works now! */
	netfd = open_or_die("/dev/net/tun", O_RDWR);
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strcpy(ifr.ifr_name, "tap%d");
	if (ioctl(netfd, TUNSETIFF, &ifr) != 0)
		err(1, "configuring /dev/net/tun");
	/* We don't need checksums calculated for packets coming in this
	 * device: trust us! */
	ioctl(netfd, TUNSETNOCSUM, 1);

	/* We create the net device with 1 page, using the features field of
	 * the descriptor to tell the Guest it is in slot 1 (NET_PEERNUM), and
	 * that the device has fairly random timing.  We do *not* specify
	 * LGUEST_NET_F_NOCSUM: these packets can reach the real world.
	 *
	 * We will put our MAC address is slot 0 for the Guest to see, so
	 * it will send packets to us using the key "peer_offset(0)": */
	dev = new_device(devices, LGUEST_DEVICE_T_NET, 1,
			 NET_PEERNUM|LGUEST_DEVICE_F_RANDOMNESS, netfd,
			 handle_tun_input, peer_offset(0), handle_tun_output);

	/* We keep a flag which says whether we've seen packets come out from
	 * this network device. */
	dev->priv = malloc(sizeof(bool));
	*(bool *)dev->priv = false;

	/* We need a socket to perform the magic network ioctls to bring up the
	 * tap interface, connect to the bridge etc.  Any socket will do! */
	ipfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (ipfd < 0)
		err(1, "opening IP socket");

	/* If the command line was --tunnet=bridge:<name> do bridging. */
	if (!strncmp(BRIDGE_PFX, arg, strlen(BRIDGE_PFX))) {
		ip = INADDR_ANY;
		br_name = arg + strlen(BRIDGE_PFX);
		add_to_bridge(ipfd, ifr.ifr_name, br_name);
	} else /* It is an IP address to set up the device with */
		ip = str2ip(arg);

	/* We are peer 0, ie. first slot, so we hand dev->mem to this routine
	 * to write the MAC address at the start of the device memory.  */
	configure_device(ipfd, ifr.ifr_name, ip, dev->mem);

	/* Set "promisc" bit: we want every single packet if we're going to
	 * bridge to other machines (and otherwise it doesn't matter). */
	*((u8 *)dev->mem) |= 0x1;

	close(ipfd);

	verbose("device %p: tun net %u.%u.%u.%u\n",
		(void *)(dev->desc->pfn * getpagesize()),
		(u8)(ip>>24), (u8)(ip>>16), (u8)(ip>>8), (u8)ip);
	if (br_name)
		verbose("attached to bridge: %s\n", br_name);
}
/* That's the end of device setup. */

/*L:220 Finally we reach the core of the Launcher, which runs the Guest, serves
 * its input and output, and finally, lays it to rest. */
static void __attribute__((noreturn))
run_guest(int lguest_fd, struct device_list *device_list)
{
	for (;;) {
		u32 args[] = { LHREQ_BREAK, 0 };
		unsigned long arr[2];
		int readval;

		/* We read from the /dev/lguest device to run the Guest. */
		readval = read(lguest_fd, arr, sizeof(arr));

		/* The read can only really return sizeof(arr) (the Guest did a
		 * SEND_DMA to us), or an error. */

		/* For a successful read, arr[0] is the address of the "struct
		 * lguest_dma", and arr[1] is the key the Guest sent to. */
		if (readval == sizeof(arr)) {
			handle_output(lguest_fd, arr[0], arr[1], device_list);
			continue;
		/* ENOENT means the Guest died.  Reading tells us why. */
		} else if (errno == ENOENT) {
			char reason[1024] = { 0 };
			read(lguest_fd, reason, sizeof(reason)-1);
			errx(1, "%s", reason);
		/* EAGAIN means the waker wanted us to look at some input.
		 * Anything else means a bug or incompatible change. */
		} else if (errno != EAGAIN)
			err(1, "Running guest failed");

		/* Service input, then unset the BREAK which releases
		 * the Waker. */
		handle_input(lguest_fd, device_list);
		if (write(lguest_fd, args, sizeof(args)) < 0)
			err(1, "Resetting break");
	}
}
/*
 * This is the end of the Launcher.
 *
 * But wait!  We've seen I/O from the Launcher, and we've seen I/O from the
 * Drivers.  If we were to see the Host kernel I/O code, our understanding
 * would be complete... :*/

static struct option opts[] = {
	{ "verbose", 0, NULL, 'v' },
	{ "sharenet", 1, NULL, 's' },
	{ "tunnet", 1, NULL, 't' },
	{ "block", 1, NULL, 'b' },
	{ "initrd", 1, NULL, 'i' },
	{ NULL },
};
static void usage(void)
{
	errx(1, "Usage: lguest [--verbose] "
	     "[--sharenet=<filename>|--tunnet=(<ipaddr>|bridge:<bridgename>)\n"
	     "|--block=<filename>|--initrd=<filename>]...\n"
	     "<mem-in-mb> vmlinux [args...]");
}

/*L:100 The Launcher code itself takes us out into userspace, that scary place
 * where pointers run wild and free!  Unfortunately, like most userspace
 * programs, it's quite boring (which is why everyone like to hack on the
 * kernel!).  Perhaps if you make up an Lguest Drinking Game at this point, it
 * will get you through this section.  Or, maybe not.
 *
 * The Launcher binary sits up high, usually starting at address 0xB8000000.
 * Everything below this is the "physical" memory for the Guest.  For example,
 * if the Guest were to write a "1" at physical address 0, we would see a "1"
 * in the Launcher at "(int *)0".  Guest physical == Launcher virtual.
 *
 * This can be tough to get your head around, but usually it just means that we
 * don't need to do any conversion when the Guest gives us it's "physical"
 * addresses.
 */
int main(int argc, char *argv[])
{
	/* Memory, top-level pagetable, code startpoint, PAGE_OFFSET and size
	 * of the (optional) initrd. */
	unsigned long mem = 0, pgdir, start, page_offset, initrd_size = 0;
	/* A temporary and the /dev/lguest file descriptor. */
	int i, c, lguest_fd;
	/* The list of Guest devices, based on command line arguments. */
	struct device_list device_list;
	/* The boot information for the Guest: at guest-physical address 0. */
	void *boot = (void *)0;
	/* If they specify an initrd file to load. */
	const char *initrd_name = NULL;

	/* First we initialize the device list.  Since console and network
	 * device receive input from a file descriptor, we keep an fdset
	 * (infds) and the maximum fd number (max_infd) with the head of the
	 * list.  We also keep a pointer to the last device, for easy appending
	 * to the list. */
	device_list.max_infd = -1;
	device_list.dev = NULL;
	device_list.lastdev = &device_list.dev;
	FD_ZERO(&device_list.infds);

	/* We need to know how much memory so we can set up the device
	 * descriptor and memory pages for the devices as we parse the command
	 * line.  So we quickly look through the arguments to find the amount
	 * of memory now. */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			mem = top = atoi(argv[i]) * 1024 * 1024;
			device_list.descs = map_zeroed_pages(top, 1);
			top += getpagesize();
			break;
		}
	}

	/* The options are fairly straight-forward */
	while ((c = getopt_long(argc, argv, "v", opts, NULL)) != EOF) {
		switch (c) {
		case 'v':
			verbose = true;
			break;
		case 's':
			setup_net_file(optarg, &device_list);
			break;
		case 't':
			setup_tun_net(optarg, &device_list);
			break;
		case 'b':
			setup_block_file(optarg, &device_list);
			break;
		case 'i':
			initrd_name = optarg;
			break;
		default:
			warnx("Unknown argument %s", argv[optind]);
			usage();
		}
	}
	/* After the other arguments we expect memory and kernel image name,
	 * followed by command line arguments for the kernel. */
	if (optind + 2 > argc)
		usage();

	/* We always have a console device */
	setup_console(&device_list);

	/* We start by mapping anonymous pages over all of guest-physical
	 * memory range.  This fills it with 0, and ensures that the Guest
	 * won't be killed when it tries to access it. */
	map_zeroed_pages(0, mem / getpagesize());

	/* Now we load the kernel */
	start = load_kernel(open_or_die(argv[optind+1], O_RDONLY),
			    &page_offset);

	/* Map the initrd image if requested (at top of physical memory) */
	if (initrd_name) {
		initrd_size = load_initrd(initrd_name, mem);
		/* These are the location in the Linux boot header where the
		 * start and size of the initrd are expected to be found. */
		*(unsigned long *)(boot+0x218) = mem - initrd_size;
		*(unsigned long *)(boot+0x21c) = initrd_size;
		/* The bootloader type 0xFF means "unknown"; that's OK. */
		*(unsigned char *)(boot+0x210) = 0xFF;
	}

	/* Set up the initial linear pagetables, starting below the initrd. */
	pgdir = setup_pagetables(mem, initrd_size, page_offset);

	/* The Linux boot header contains an "E820" memory map: ours is a
	 * simple, single region. */
	*(char*)(boot+E820NR) = 1;
	*((struct e820entry *)(boot+E820MAP))
		= ((struct e820entry) { 0, mem, E820_RAM });
	/* The boot header contains a command line pointer: we put the command
	 * line after the boot header (at address 4096) */
	*(void **)(boot + 0x228) = boot + 4096;
	concat(boot + 4096, argv+optind+2);

	/* The guest type value of "1" tells the Guest it's under lguest. */
	*(int *)(boot + 0x23c) = 1;

	/* We tell the kernel to initialize the Guest: this returns the open
	 * /dev/lguest file descriptor. */
	lguest_fd = tell_kernel(pgdir, start, page_offset);

	/* We fork off a child process, which wakes the Launcher whenever one
	 * of the input file descriptors needs attention.  Otherwise we would
	 * run the Guest until it tries to output something. */
	waker_fd = setup_waker(lguest_fd, &device_list);

	/* Finally, run the Guest.  This doesn't return. */
	run_guest(lguest_fd, &device_list);
}
