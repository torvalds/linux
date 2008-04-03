/*P:100 This is the Launcher code, a simple program which lays out the
 * "physical" memory for the new Guest by mapping the kernel image and
 * the virtual devices, then opens /dev/lguest to tell the kernel
 * about the Guest and control it. :*/
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
#include <sys/param.h>
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
#include <assert.h>
#include <sched.h>
#include <limits.h>
#include <stddef.h>
#include "linux/lguest_launcher.h"
#include "linux/virtio_config.h"
#include "linux/virtio_net.h"
#include "linux/virtio_blk.h"
#include "linux/virtio_console.h"
#include "linux/virtio_ring.h"
#include "asm-x86/bootparam.h"
/*L:110 We can ignore the 39 include files we need for this program, but I do
 * want to draw attention to the use of kernel-style types.
 *
 * As Linus said, "C is a Spartan language, and so should your naming be."  I
 * like these abbreviations, so we define them here.  Note that u64 is always
 * unsigned long long, which works on all Linux systems: this means that we can
 * use %llu in printf for any u64. */
typedef unsigned long long u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
/*:*/

#define PAGE_PRESENT 0x7 	/* Present, RW, Execute */
#define NET_PEERNUM 1
#define BRIDGE_PFX "bridge:"
#ifndef SIOCBRADDIF
#define SIOCBRADDIF	0x89a2		/* add interface to bridge      */
#endif
/* We can have up to 256 pages for devices. */
#define DEVICE_PAGES 256
/* This will occupy 2 pages: it must be a power of 2. */
#define VIRTQUEUE_NUM 128

/*L:120 verbose is both a global flag and a macro.  The C preprocessor allows
 * this, and although I wouldn't recommend it, it works quite nicely here. */
static bool verbose;
#define verbose(args...) \
	do { if (verbose) printf(args); } while(0)
/*:*/

/* The pipe to send commands to the waker process */
static int waker_fd;
/* The pointer to the start of guest memory. */
static void *guest_base;
/* The maximum guest physical address allowed, and maximum possible. */
static unsigned long guest_limit, guest_max;

/* a per-cpu variable indicating whose vcpu is currently running */
static unsigned int __thread cpu_id;

/* This is our list of devices. */
struct device_list
{
	/* Summary information about the devices in our list: ready to pass to
	 * select() to ask which need servicing.*/
	fd_set infds;
	int max_infd;

	/* Counter to assign interrupt numbers. */
	unsigned int next_irq;

	/* Counter to print out convenient device numbers. */
	unsigned int device_num;

	/* The descriptor page for the devices. */
	u8 *descpage;

	/* A single linked list of devices. */
	struct device *dev;
	/* And a pointer to the last device for easy append and also for
	 * configuration appending. */
	struct device *lastdev;
};

/* The list of Guest devices, based on command line arguments. */
static struct device_list devices;

/* The device structure describes a single device. */
struct device
{
	/* The linked-list pointer. */
	struct device *next;

	/* The this device's descriptor, as mapped into the Guest. */
	struct lguest_device_desc *desc;

	/* The name of this device, for --verbose. */
	const char *name;

	/* If handle_input is set, it wants to be called when this file
	 * descriptor is ready. */
	int fd;
	bool (*handle_input)(int fd, struct device *me);

	/* Any queues attached to this device */
	struct virtqueue *vq;

	/* Device-specific data. */
	void *priv;
};

/* The virtqueue structure describes a queue attached to a device. */
struct virtqueue
{
	struct virtqueue *next;

	/* Which device owns me. */
	struct device *dev;

	/* The configuration for this queue. */
	struct lguest_vqconfig config;

	/* The actual ring of buffers. */
	struct vring vring;

	/* Last available index we saw. */
	u16 last_avail_idx;

	/* The routine to call when the Guest pings us. */
	void (*handle_output)(int fd, struct virtqueue *me);
};

/* Remember the arguments to the program so we can "reboot" */
static char **main_args;

/* Since guest is UP and we don't run at the same time, we don't need barriers.
 * But I include them in the code in case others copy it. */
#define wmb()

/* Convert an iovec element to the given type.
 *
 * This is a fairly ugly trick: we need to know the size of the type and
 * alignment requirement to check the pointer is kosher.  It's also nice to
 * have the name of the type in case we report failure.
 *
 * Typing those three things all the time is cumbersome and error prone, so we
 * have a macro which sets them all up and passes to the real function. */
#define convert(iov, type) \
	((type *)_convert((iov), sizeof(type), __alignof__(type), #type))

static void *_convert(struct iovec *iov, size_t size, size_t align,
		      const char *name)
{
	if (iov->iov_len != size)
		errx(1, "Bad iovec size %zu for %s", iov->iov_len, name);
	if ((unsigned long)iov->iov_base % align != 0)
		errx(1, "Bad alignment %p for %s", iov->iov_base, name);
	return iov->iov_base;
}

/* The virtio configuration space is defined to be little-endian.  x86 is
 * little-endian too, but it's nice to be explicit so we have these helpers. */
#define cpu_to_le16(v16) (v16)
#define cpu_to_le32(v32) (v32)
#define cpu_to_le64(v64) (v64)
#define le16_to_cpu(v16) (v16)
#define le32_to_cpu(v32) (v32)
#define le64_to_cpu(v64) (v64)

/* The device virtqueue descriptors are followed by feature bitmasks. */
static u8 *get_feature_bits(struct device *dev)
{
	return (u8 *)(dev->desc + 1)
		+ dev->desc->num_vq * sizeof(struct lguest_vqconfig);
}

/*L:100 The Launcher code itself takes us out into userspace, that scary place
 * where pointers run wild and free!  Unfortunately, like most userspace
 * programs, it's quite boring (which is why everyone likes to hack on the
 * kernel!).  Perhaps if you make up an Lguest Drinking Game at this point, it
 * will get you through this section.  Or, maybe not.
 *
 * The Launcher sets up a big chunk of memory to be the Guest's "physical"
 * memory and stores it in "guest_base".  In other words, Guest physical ==
 * Launcher virtual with an offset.
 *
 * This can be tough to get your head around, but usually it just means that we
 * use these trivial conversion functions when the Guest gives us it's
 * "physical" addresses: */
static void *from_guest_phys(unsigned long addr)
{
	return guest_base + addr;
}

static unsigned long to_guest_phys(const void *addr)
{
	return (addr - guest_base);
}

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

/* map_zeroed_pages() takes a number of pages. */
static void *map_zeroed_pages(unsigned int num)
{
	int fd = open_or_die("/dev/zero", O_RDONLY);
	void *addr;

	/* We use a private mapping (ie. if we write to the page, it will be
	 * copied). */
	addr = mmap(NULL, getpagesize() * num,
		    PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED)
		err(1, "Mmaping %u pages of /dev/zero", num);

	return addr;
}

/* Get some more pages for a device. */
static void *get_pages(unsigned int num)
{
	void *addr = from_guest_phys(guest_limit);

	guest_limit += num * getpagesize();
	if (guest_limit > guest_max)
		errx(1, "Not enough memory for devices");
	return addr;
}

/* This routine is used to load the kernel or initrd.  It tries mmap, but if
 * that fails (Plan 9's kernel file isn't nicely aligned on page boundaries),
 * it falls back to reading the memory in. */
static void map_at(int fd, void *addr, unsigned long offset, unsigned long len)
{
	ssize_t r;

	/* We map writable even though for some segments are marked read-only.
	 * The kernel really wants to be writable: it patches its own
	 * instructions.
	 *
	 * MAP_PRIVATE means that the page won't be copied until a write is
	 * done to it.  This allows us to share untouched memory between
	 * Guests. */
	if (mmap(addr, len, PROT_READ|PROT_WRITE|PROT_EXEC,
		 MAP_FIXED|MAP_PRIVATE, fd, offset) != MAP_FAILED)
		return;

	/* pread does a seek and a read in one shot: saves a few lines. */
	r = pread(fd, addr, len, offset);
	if (r != len)
		err(1, "Reading offset %lu len %lu gave %zi", offset, len, r);
}

/* This routine takes an open vmlinux image, which is in ELF, and maps it into
 * the Guest memory.  ELF = Embedded Linking Format, which is the format used
 * by all modern binaries on Linux including the kernel.
 *
 * The ELF headers give *two* addresses: a physical address, and a virtual
 * address.  We use the physical address; the Guest will map itself to the
 * virtual address.
 *
 * We return the starting address. */
static unsigned long map_elf(int elf_fd, const Elf32_Ehdr *ehdr)
{
	Elf32_Phdr phdr[ehdr->e_phnum];
	unsigned int i;

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

	/* Try all the headers: there are usually only three.  A read-only one,
	 * a read-write one, and a "note" section which we don't load. */
	for (i = 0; i < ehdr->e_phnum; i++) {
		/* If this isn't a loadable segment, we ignore it */
		if (phdr[i].p_type != PT_LOAD)
			continue;

		verbose("Section %i: size %i addr %p\n",
			i, phdr[i].p_memsz, (void *)phdr[i].p_paddr);

		/* We map this section of the file at its physical address. */
		map_at(elf_fd, from_guest_phys(phdr[i].p_paddr),
		       phdr[i].p_offset, phdr[i].p_filesz);
	}

	/* The entry point is given in the ELF header. */
	return ehdr->e_entry;
}

/*L:150 A bzImage, unlike an ELF file, is not meant to be loaded.  You're
 * supposed to jump into it and it will unpack itself.  We used to have to
 * perform some hairy magic because the unpacking code scared me.
 *
 * Fortunately, Jeremy Fitzhardinge convinced me it wasn't that hard and wrote
 * a small patch to jump over the tricky bits in the Guest, so now we just read
 * the funky header so we know where in the file to load, and away we go! */
static unsigned long load_bzimage(int fd)
{
	struct boot_params boot;
	int r;
	/* Modern bzImages get loaded at 1M. */
	void *p = from_guest_phys(0x100000);

	/* Go back to the start of the file and read the header.  It should be
	 * a Linux boot header (see Documentation/i386/boot.txt) */
	lseek(fd, 0, SEEK_SET);
	read(fd, &boot, sizeof(boot));

	/* Inside the setup_hdr, we expect the magic "HdrS" */
	if (memcmp(&boot.hdr.header, "HdrS", 4) != 0)
		errx(1, "This doesn't look like a bzImage to me");

	/* Skip over the extra sectors of the header. */
	lseek(fd, (boot.hdr.setup_sects+1) * 512, SEEK_SET);

	/* Now read everything into memory. in nice big chunks. */
	while ((r = read(fd, p, 65536)) > 0)
		p += r;

	/* Finally, code32_start tells us where to enter the kernel. */
	return boot.hdr.code32_start;
}

/*L:140 Loading the kernel is easy when it's a "vmlinux", but most kernels
 * come wrapped up in the self-decompressing "bzImage" format.  With a little
 * work, we can load those, too. */
static unsigned long load_kernel(int fd)
{
	Elf32_Ehdr hdr;

	/* Read in the first few bytes. */
	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		err(1, "Reading kernel");

	/* If it's an ELF file, it starts with "\177ELF" */
	if (memcmp(hdr.e_ident, ELFMAG, SELFMAG) == 0)
		return map_elf(fd, &hdr);

	/* Otherwise we assume it's a bzImage, and try to load it. */
	return load_bzimage(fd);
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

	ifd = open_or_die(name, O_RDONLY);
	/* fstat() is needed to get the file size. */
	if (fstat(ifd, &st) < 0)
		err(1, "fstat() on initrd '%s'", name);

	/* We map the initrd at the top of memory, but mmap wants it to be
	 * page-aligned, so we round the size up for that. */
	len = page_align(st.st_size);
	map_at(ifd, from_guest_phys(mem - len), 0, st.st_size);
	/* Once a file is mapped, you can close the file descriptor.  It's a
	 * little odd, but quite useful. */
	close(ifd);
	verbose("mapped initrd %s size=%lu @ %p\n", name, len, (void*)mem-len);

	/* We return the initrd size. */
	return len;
}

/* Once we know how much memory we have we can construct simple linear page
 * tables which set virtual == physical which will get the Guest far enough
 * into the boot to create its own.
 *
 * We lay them out of the way, just below the initrd (which is why we need to
 * know its size here). */
static unsigned long setup_pagetables(unsigned long mem,
				      unsigned long initrd_size)
{
	unsigned long *pgdir, *linear;
	unsigned int mapped_pages, i, linear_pages;
	unsigned int ptes_per_page = getpagesize()/sizeof(void *);

	mapped_pages = mem/getpagesize();

	/* Each PTE page can map ptes_per_page pages: how many do we need? */
	linear_pages = (mapped_pages + ptes_per_page-1)/ptes_per_page;

	/* We put the toplevel page directory page at the top of memory. */
	pgdir = from_guest_phys(mem) - initrd_size - getpagesize();

	/* Now we use the next linear_pages pages as pte pages */
	linear = (void *)pgdir - linear_pages*getpagesize();

	/* Linear mapping is easy: put every page's address into the mapping in
	 * order.  PAGE_PRESENT contains the flags Present, Writable and
	 * Executable. */
	for (i = 0; i < mapped_pages; i++)
		linear[i] = ((i * getpagesize()) | PAGE_PRESENT);

	/* The top level points to the linear page table pages above. */
	for (i = 0; i < mapped_pages; i += ptes_per_page) {
		pgdir[i/ptes_per_page]
			= ((to_guest_phys(linear) + i*sizeof(void *))
			   | PAGE_PRESENT);
	}

	verbose("Linear mapping of %u pages in %u pte pages at %#lx\n",
		mapped_pages, linear_pages, to_guest_phys(linear));

	/* We return the top level (guest-physical) address: the kernel needs
	 * to know where it is. */
	return to_guest_phys(pgdir);
}
/*:*/

/* Simple routine to roll all the commandline arguments together with spaces
 * between them. */
static void concat(char *dst, char *args[])
{
	unsigned int i, len = 0;

	for (i = 0; args[i]; i++) {
		if (i) {
			strcat(dst+len, " ");
			len++;
		}
		strcpy(dst+len, args[i]);
		len += strlen(args[i]);
	}
	/* In case it's empty. */
	dst[len] = '\0';
}

/*L:185 This is where we actually tell the kernel to initialize the Guest.  We
 * saw the arguments it expects when we looked at initialize() in lguest_user.c:
 * the base of Guest "physical" memory, the top physical page to allow, the
 * top level pagetable and the entry point for the Guest. */
static int tell_kernel(unsigned long pgdir, unsigned long start)
{
	unsigned long args[] = { LHREQ_INITIALIZE,
				 (unsigned long)guest_base,
				 guest_limit / getpagesize(), pgdir, start };
	int fd;

	verbose("Guest: %p - %p (%#lx)\n",
		guest_base, guest_base + guest_limit, guest_limit);
	fd = open_or_die("/dev/lguest", O_RDWR);
	if (write(fd, args, sizeof(args)) < 0)
		err(1, "Writing to /dev/lguest");

	/* We return the /dev/lguest file descriptor to control this Guest */
	return fd;
}
/*:*/

static void add_device_fd(int fd)
{
	FD_SET(fd, &devices.infds);
	if (fd > devices.max_infd)
		devices.max_infd = fd;
}

/*L:200
 * The Waker.
 *
 * With console, block and network devices, we can have lots of input which we
 * need to process.  We could try to tell the kernel what file descriptors to
 * watch, but handing a file descriptor mask through to the kernel is fairly
 * icky.
 *
 * Instead, we fork off a process which watches the file descriptors and writes
 * the LHREQ_BREAK command to the /dev/lguest file descriptor to tell the Host
 * stop running the Guest.  This causes the Launcher to return from the
 * /dev/lguest read with -EAGAIN, where it will write to /dev/lguest to reset
 * the LHREQ_BREAK and wake us up again.
 *
 * This, of course, is merely a different *kind* of icky.
 */
static void wake_parent(int pipefd, int lguest_fd)
{
	/* Add the pipe from the Launcher to the fdset in the device_list, so
	 * we watch it, too. */
	add_device_fd(pipefd);

	for (;;) {
		fd_set rfds = devices.infds;
		unsigned long args[] = { LHREQ_BREAK, 1 };

		/* Wait until input is ready from one of the devices. */
		select(devices.max_infd+1, &rfds, NULL, NULL, NULL);
		/* Is it a message from the Launcher? */
		if (FD_ISSET(pipefd, &rfds)) {
			int fd;
			/* If read() returns 0, it means the Launcher has
			 * exited.  We silently follow. */
			if (read(pipefd, &fd, sizeof(fd)) == 0)
				exit(0);
			/* Otherwise it's telling us to change what file
			 * descriptors we're to listen to.  Positive means
			 * listen to a new one, negative means stop
			 * listening. */
			if (fd >= 0)
				FD_SET(fd, &devices.infds);
			else
				FD_CLR(-fd - 1, &devices.infds);
		} else /* Send LHREQ_BREAK command. */
			pwrite(lguest_fd, args, sizeof(args), cpu_id);
	}
}

/* This routine just sets up a pipe to the Waker process. */
static int setup_waker(int lguest_fd)
{
	int pipefd[2], child;

	/* We create a pipe to talk to the Waker, and also so it knows when the
	 * Launcher dies (and closes pipe). */
	pipe(pipefd);
	child = fork();
	if (child == -1)
		err(1, "forking");

	if (child == 0) {
		/* We are the Waker: close the "writing" end of our copy of the
		 * pipe and start waiting for input. */
		close(pipefd[1]);
		wake_parent(pipefd[0], lguest_fd);
	}
	/* Close the reading end of our copy of the pipe. */
	close(pipefd[0]);

	/* Here is the fd used to talk to the waker. */
	return pipefd[1];
}

/*
 * Device Handling.
 *
 * When the Guest gives us a buffer, it sends an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which checks it and exits with an error message
 * if something funny is going on:
 */
static void *_check_pointer(unsigned long addr, unsigned int size,
			    unsigned int line)
{
	/* We have to separately check addr and addr+size, because size could
	 * be huge and addr + size might wrap around. */
	if (addr >= guest_limit || addr + size >= guest_limit)
		errx(1, "%s:%i: Invalid address %#lx", __FILE__, line, addr);
	/* We return a pointer for the caller's convenience, now we know it's
	 * safe to use. */
	return from_guest_phys(addr);
}
/* A macro which transparently hands the line number to the real function. */
#define check_pointer(addr,size) _check_pointer(addr, size, __LINE__)

/* Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or vq->vring.num if we're
 * at the end. */
static unsigned next_desc(struct virtqueue *vq, unsigned int i)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(vq->vring.desc[i].flags & VRING_DESC_F_NEXT))
		return vq->vring.num;

	/* Check they're not leading us off end of descriptors. */
	next = vq->vring.desc[i].next;
	/* Make sure compiler knows to grab that: we don't want it changing! */
	wmb();

	if (next >= vq->vring.num)
		errx(1, "Desc next is %u", next);

	return next;
}

/* This looks in the virtqueue and for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function returns the descriptor number found, or vq->vring.num (which
 * is never a valid descriptor number) if none was found. */
static unsigned get_vq_desc(struct virtqueue *vq,
			    struct iovec iov[],
			    unsigned int *out_num, unsigned int *in_num)
{
	unsigned int i, head;

	/* Check it isn't doing very strange things with descriptor numbers. */
	if ((u16)(vq->vring.avail->idx - vq->last_avail_idx) > vq->vring.num)
		errx(1, "Guest moved used index from %u to %u",
		     vq->last_avail_idx, vq->vring.avail->idx);

	/* If there's nothing new since last we looked, return invalid. */
	if (vq->vring.avail->idx == vq->last_avail_idx)
		return vq->vring.num;

	/* Grab the next descriptor number they're advertising, and increment
	 * the index we've seen. */
	head = vq->vring.avail->ring[vq->last_avail_idx++ % vq->vring.num];

	/* If their number is silly, that's a fatal mistake. */
	if (head >= vq->vring.num)
		errx(1, "Guest says index %u is available", head);

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;

	i = head;
	do {
		/* Grab the first descriptor, and check it's OK. */
		iov[*out_num + *in_num].iov_len = vq->vring.desc[i].len;
		iov[*out_num + *in_num].iov_base
			= check_pointer(vq->vring.desc[i].addr,
					vq->vring.desc[i].len);
		/* If this is an input descriptor, increment that count. */
		if (vq->vring.desc[i].flags & VRING_DESC_F_WRITE)
			(*in_num)++;
		else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			if (*in_num)
				errx(1, "Descriptor has out after in");
			(*out_num)++;
		}

		/* If we've got too many, that implies a descriptor loop. */
		if (*out_num + *in_num > vq->vring.num)
			errx(1, "Looped descriptor");
	} while ((i = next_desc(vq, i)) != vq->vring.num);

	return head;
}

/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to send them an interrupt, using trigger_irq(). */
static void add_used(struct virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem *used;

	/* The virtqueue contains a ring of used buffers.  Get a pointer to the
	 * next entry in that used ring. */
	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = head;
	used->len = len;
	/* Make sure buffer is written before we update index. */
	wmb();
	vq->vring.used->idx++;
}

/* This actually sends the interrupt for this virtqueue */
static void trigger_irq(int fd, struct virtqueue *vq)
{
	unsigned long buf[] = { LHREQ_IRQ, vq->config.irq };

	/* If they don't want an interrupt, don't send one. */
	if (vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)
		return;

	/* Send the Guest an interrupt tell them we used something up. */
	if (write(fd, buf, sizeof(buf)) != 0)
		err(1, "Triggering irq %i", vq->config.irq);
}

/* And here's the combo meal deal.  Supersize me! */
static void add_used_and_trigger(int fd, struct virtqueue *vq,
				 unsigned int head, int len)
{
	add_used(vq, head, len);
	trigger_irq(fd, vq);
}

/*
 * The Console
 *
 * Here is the input terminal setting we save, and the routine to restore them
 * on exit so the user gets their terminal back. */
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
	int len;
	unsigned int head, in_num, out_num;
	struct iovec iov[dev->vq->vring.num];
	struct console_abort *abort = dev->priv;

	/* First we need a console buffer from the Guests's input virtqueue. */
	head = get_vq_desc(dev->vq, iov, &out_num, &in_num);

	/* If they're not ready for input, stop listening to this file
	 * descriptor.  We'll start again once they add an input buffer. */
	if (head == dev->vq->vring.num)
		return false;

	if (out_num)
		errx(1, "Output buffers in console in queue?");

	/* This is why we convert to iovecs: the readv() call uses them, and so
	 * it reads straight into the Guest's buffer. */
	len = readv(dev->fd, iov, in_num);
	if (len <= 0) {
		/* This implies that the console is closed, is /dev/null, or
		 * something went terribly wrong. */
		warnx("Failed to get console input, ignoring console.");
		/* Put the input terminal back. */
		restore_term();
		/* Remove callback from input vq, so it doesn't restart us. */
		dev->vq->handle_output = NULL;
		/* Stop listening to this fd: don't call us again. */
		return false;
	}

	/* Tell the Guest about the new input. */
	add_used_and_trigger(fd, dev->vq, head, len);

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
				unsigned long args[] = { LHREQ_BREAK, 0 };
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

	/* Everything went OK! */
	return true;
}

/* Handling output for console is simple: we just get all the output buffers
 * and write them to stdout. */
static void handle_console_output(int fd, struct virtqueue *vq)
{
	unsigned int head, out, in;
	int len;
	struct iovec iov[vq->vring.num];

	/* Keep getting output buffers from the Guest until we run out. */
	while ((head = get_vq_desc(vq, iov, &out, &in)) != vq->vring.num) {
		if (in)
			errx(1, "Input buffers in output queue?");
		len = writev(STDOUT_FILENO, iov, out);
		add_used_and_trigger(fd, vq, head, len);
	}
}

/*
 * The Network
 *
 * Handling output for network is also simple: we get all the output buffers
 * and write them (ignoring the first element) to this device's file descriptor
 * (/dev/net/tun).
 */
static void handle_net_output(int fd, struct virtqueue *vq)
{
	unsigned int head, out, in;
	int len;
	struct iovec iov[vq->vring.num];

	/* Keep getting output buffers from the Guest until we run out. */
	while ((head = get_vq_desc(vq, iov, &out, &in)) != vq->vring.num) {
		if (in)
			errx(1, "Input buffers in output queue?");
		/* Check header, but otherwise ignore it (we told the Guest we
		 * supported no features, so it shouldn't have anything
		 * interesting). */
		(void)convert(&iov[0], struct virtio_net_hdr);
		len = writev(vq->dev->fd, iov+1, out-1);
		add_used_and_trigger(fd, vq, head, len);
	}
}

/* This is where we handle a packet coming in from the tun device to our
 * Guest. */
static bool handle_tun_input(int fd, struct device *dev)
{
	unsigned int head, in_num, out_num;
	int len;
	struct iovec iov[dev->vq->vring.num];
	struct virtio_net_hdr *hdr;

	/* First we need a network buffer from the Guests's recv virtqueue. */
	head = get_vq_desc(dev->vq, iov, &out_num, &in_num);
	if (head == dev->vq->vring.num) {
		/* Now, it's expected that if we try to send a packet too
		 * early, the Guest won't be ready yet.  Wait until the device
		 * status says it's ready. */
		/* FIXME: Actually want DRIVER_ACTIVE here. */
		if (dev->desc->status & VIRTIO_CONFIG_S_DRIVER_OK)
			warn("network: no dma buffer!");
		/* We'll turn this back on if input buffers are registered. */
		return false;
	} else if (out_num)
		errx(1, "Output buffers in network recv queue?");

	/* First element is the header: we set it to 0 (no features). */
	hdr = convert(&iov[0], struct virtio_net_hdr);
	hdr->flags = 0;
	hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;

	/* Read the packet from the device directly into the Guest's buffer. */
	len = readv(dev->fd, iov+1, in_num-1);
	if (len <= 0)
		err(1, "reading network");

	/* Tell the Guest about the new packet. */
	add_used_and_trigger(fd, dev->vq, head, sizeof(*hdr) + len);

	verbose("tun input packet len %i [%02x %02x] (%s)\n", len,
		((u8 *)iov[1].iov_base)[0], ((u8 *)iov[1].iov_base)[1],
		head != dev->vq->vring.num ? "sent" : "discarded");

	/* All good. */
	return true;
}

/*L:215 This is the callback attached to the network and console input
 * virtqueues: it ensures we try again, in case we stopped console or net
 * delivery because Guest didn't have any buffers. */
static void enable_fd(int fd, struct virtqueue *vq)
{
	add_device_fd(vq->dev->fd);
	/* Tell waker to listen to it again */
	write(waker_fd, &vq->dev->fd, sizeof(vq->dev->fd));
}

/* When the Guest asks us to reset a device, it's is fairly easy. */
static void reset_device(struct device *dev)
{
	struct virtqueue *vq;

	verbose("Resetting device %s\n", dev->name);
	/* Clear the status. */
	dev->desc->status = 0;

	/* Clear any features they've acked. */
	memset(get_feature_bits(dev) + dev->desc->feature_len, 0,
	       dev->desc->feature_len);

	/* Zero out the virtqueues. */
	for (vq = dev->vq; vq; vq = vq->next) {
		memset(vq->vring.desc, 0,
		       vring_size(vq->config.num, getpagesize()));
		vq->last_avail_idx = 0;
	}
}

/* This is the generic routine we call when the Guest uses LHCALL_NOTIFY. */
static void handle_output(int fd, unsigned long addr)
{
	struct device *i;
	struct virtqueue *vq;

	/* Check each device and virtqueue. */
	for (i = devices.dev; i; i = i->next) {
		/* Notifications to device descriptors reset the device. */
		if (from_guest_phys(addr) == i->desc) {
			reset_device(i);
			return;
		}

		/* Notifications to virtqueues mean output has occurred. */
		for (vq = i->vq; vq; vq = vq->next) {
			if (vq->config.pfn != addr/getpagesize())
				continue;

			/* Guest should acknowledge (and set features!)  before
			 * using the device. */
			if (i->desc->status == 0) {
				warnx("%s gave early output", i->name);
				return;
			}

			if (strcmp(vq->dev->name, "console") != 0)
				verbose("Output to %s\n", vq->dev->name);
			if (vq->handle_output)
				vq->handle_output(fd, vq);
			return;
		}
	}

	/* Early console write is done using notify on a nul-terminated string
	 * in Guest memory. */
	if (addr >= guest_limit)
		errx(1, "Bad NOTIFY %#lx", addr);

	write(STDOUT_FILENO, from_guest_phys(addr),
	      strnlen(from_guest_phys(addr), guest_limit - addr));
}

/* This is called when the Waker wakes us up: check for incoming file
 * descriptors. */
static void handle_input(int fd)
{
	/* select() wants a zeroed timeval to mean "don't wait". */
	struct timeval poll = { .tv_sec = 0, .tv_usec = 0 };

	for (;;) {
		struct device *i;
		fd_set fds = devices.infds;

		/* If nothing is ready, we're done. */
		if (select(devices.max_infd+1, &fds, NULL, NULL, &poll) == 0)
			break;

		/* Otherwise, call the device(s) which have readable file
		 * descriptors and a method of handling them.  */
		for (i = devices.dev; i; i = i->next) {
			if (i->handle_input && FD_ISSET(i->fd, &fds)) {
				int dev_fd;
				if (i->handle_input(fd, i))
					continue;

				/* If handle_input() returns false, it means we
				 * should no longer service it.  Networking and
				 * console do this when there's no input
				 * buffers to deliver into.  Console also uses
				 * it when it discovers that stdin is closed. */
				FD_CLR(i->fd, &devices.infds);
				/* Tell waker to ignore it too, by sending a
				 * negative fd number (-1, since 0 is a valid
				 * FD number). */
				dev_fd = -i->fd - 1;
				write(waker_fd, &dev_fd, sizeof(dev_fd));
			}
		}
	}
}

/*L:190
 * Device Setup
 *
 * All devices need a descriptor so the Guest knows it exists, and a "struct
 * device" so the Launcher can keep track of it.  We have common helper
 * routines to allocate and manage them.
 */

/* The layout of the device page is a "struct lguest_device_desc" followed by a
 * number of virtqueue descriptors, then two sets of feature bits, then an
 * array of configuration bytes.  This routine returns the configuration
 * pointer. */
static u8 *device_config(const struct device *dev)
{
	return (void *)(dev->desc + 1)
		+ dev->desc->num_vq * sizeof(struct lguest_vqconfig)
		+ dev->desc->feature_len * 2;
}

/* This routine allocates a new "struct lguest_device_desc" from descriptor
 * table page just above the Guest's normal memory.  It returns a pointer to
 * that descriptor. */
static struct lguest_device_desc *new_dev_desc(u16 type)
{
	struct lguest_device_desc d = { .type = type };
	void *p;

	/* Figure out where the next device config is, based on the last one. */
	if (devices.lastdev)
		p = device_config(devices.lastdev)
			+ devices.lastdev->desc->config_len;
	else
		p = devices.descpage;

	/* We only have one page for all the descriptors. */
	if (p + sizeof(d) > (void *)devices.descpage + getpagesize())
		errx(1, "Too many devices");

	/* p might not be aligned, so we memcpy in. */
	return memcpy(p, &d, sizeof(d));
}

/* Each device descriptor is followed by the description of its virtqueues.  We
 * specify how many descriptors the virtqueue is to have. */
static void add_virtqueue(struct device *dev, unsigned int num_descs,
			  void (*handle_output)(int fd, struct virtqueue *me))
{
	unsigned int pages;
	struct virtqueue **i, *vq = malloc(sizeof(*vq));
	void *p;

	/* First we need some memory for this virtqueue. */
	pages = (vring_size(num_descs, getpagesize()) + getpagesize() - 1)
		/ getpagesize();
	p = get_pages(pages);

	/* Initialize the virtqueue */
	vq->next = NULL;
	vq->last_avail_idx = 0;
	vq->dev = dev;

	/* Initialize the configuration. */
	vq->config.num = num_descs;
	vq->config.irq = devices.next_irq++;
	vq->config.pfn = to_guest_phys(p) / getpagesize();

	/* Initialize the vring. */
	vring_init(&vq->vring, num_descs, p, getpagesize());

	/* Append virtqueue to this device's descriptor.  We use
	 * device_config() to get the end of the device's current virtqueues;
	 * we check that we haven't added any config or feature information
	 * yet, otherwise we'd be overwriting them. */
	assert(dev->desc->config_len == 0 && dev->desc->feature_len == 0);
	memcpy(device_config(dev), &vq->config, sizeof(vq->config));
	dev->desc->num_vq++;

	verbose("Virtqueue page %#lx\n", to_guest_phys(p));

	/* Add to tail of list, so dev->vq is first vq, dev->vq->next is
	 * second.  */
	for (i = &dev->vq; *i; i = &(*i)->next);
	*i = vq;

	/* Set the routine to call when the Guest does something to this
	 * virtqueue. */
	vq->handle_output = handle_output;

	/* As an optimization, set the advisory "Don't Notify Me" flag if we
	 * don't have a handler */
	if (!handle_output)
		vq->vring.used->flags = VRING_USED_F_NO_NOTIFY;
}

/* The first half of the feature bitmask is for us to advertise features.  The
 * second half is for the Guest to accept features. */
static void add_feature(struct device *dev, unsigned bit)
{
	u8 *features = get_feature_bits(dev);

	/* We can't extend the feature bits once we've added config bytes */
	if (dev->desc->feature_len <= bit / CHAR_BIT) {
		assert(dev->desc->config_len == 0);
		dev->desc->feature_len = (bit / CHAR_BIT) + 1;
	}

	features[bit / CHAR_BIT] |= (1 << (bit % CHAR_BIT));
}

/* This routine sets the configuration fields for an existing device's
 * descriptor.  It only works for the last device, but that's OK because that's
 * how we use it. */
static void set_config(struct device *dev, unsigned len, const void *conf)
{
	/* Check we haven't overflowed our single page. */
	if (device_config(dev) + len > devices.descpage + getpagesize())
		errx(1, "Too many devices");

	/* Copy in the config information, and store the length. */
	memcpy(device_config(dev), conf, len);
	dev->desc->config_len = len;
}

/* This routine does all the creation and setup of a new device, including
 * calling new_dev_desc() to allocate the descriptor and device memory.
 *
 * See what I mean about userspace being boring? */
static struct device *new_device(const char *name, u16 type, int fd,
				 bool (*handle_input)(int, struct device *))
{
	struct device *dev = malloc(sizeof(*dev));

	/* Now we populate the fields one at a time. */
	dev->fd = fd;
	/* If we have an input handler for this file descriptor, then we add it
	 * to the device_list's fdset and maxfd. */
	if (handle_input)
		add_device_fd(dev->fd);
	dev->desc = new_dev_desc(type);
	dev->handle_input = handle_input;
	dev->name = name;
	dev->vq = NULL;

	/* Append to device list.  Prepending to a single-linked list is
	 * easier, but the user expects the devices to be arranged on the bus
	 * in command-line order.  The first network device on the command line
	 * is eth0, the first block device /dev/vda, etc. */
	if (devices.lastdev)
		devices.lastdev->next = dev;
	else
		devices.dev = dev;
	devices.lastdev = dev;

	return dev;
}

/* Our first setup routine is the console.  It's a fairly simple device, but
 * UNIX tty handling makes it uglier than it could be. */
static void setup_console(void)
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

	dev = new_device("console", VIRTIO_ID_CONSOLE,
			 STDIN_FILENO, handle_console_input);
	/* We store the console state in dev->priv, and initialize it. */
	dev->priv = malloc(sizeof(struct console_abort));
	((struct console_abort *)dev->priv)->count = 0;

	/* The console needs two virtqueues: the input then the output.  When
	 * they put something the input queue, we make sure we're listening to
	 * stdin.  When they put something in the output queue, we write it to
	 * stdout. */
	add_virtqueue(dev, VIRTQUEUE_NUM, enable_fd);
	add_virtqueue(dev, VIRTQUEUE_NUM, handle_console_output);

	verbose("device %u: console\n", devices.device_num++);
}
/*:*/

/*M:010 Inter-guest networking is an interesting area.  Simplest is to have a
 * --sharenet=<name> option which opens or creates a named pipe.  This can be
 * used to send packets to another guest in a 1:1 manner.
 *
 * More sopisticated is to use one of the tools developed for project like UML
 * to do networking.
 *
 * Faster is to do virtio bonding in kernel.  Doing this 1:1 would be
 * completely generic ("here's my vring, attach to your vring") and would work
 * for any traffic.  Of course, namespace and permissions issues need to be
 * dealt with.  A more sophisticated "multi-channel" virtio_net.c could hide
 * multiple inter-guest channels behind one interface, although it would
 * require some manner of hotplugging new virtio channels.
 *
 * Finally, we could implement a virtio network switch in the kernel. :*/

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
 * pointer. */
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

/*L:195 Our network is a Host<->Guest network.  This can either use bridging or
 * routing, but the principle is the same: it uses the "tun" device to inject
 * packets into the Host as if they came in from a normal network card.  We
 * just shunt packets between the Guest and the tun device. */
static void setup_tun_net(const char *arg)
{
	struct device *dev;
	struct ifreq ifr;
	int netfd, ipfd;
	u32 ip;
	const char *br_name = NULL;
	struct virtio_net_config conf;

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

	/* First we create a new network device. */
	dev = new_device("net", VIRTIO_ID_NET, netfd, handle_tun_input);

	/* Network devices need a receive and a send queue, just like
	 * console. */
	add_virtqueue(dev, VIRTQUEUE_NUM, enable_fd);
	add_virtqueue(dev, VIRTQUEUE_NUM, handle_net_output);

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

	/* Set up the tun device, and get the mac address for the interface. */
	configure_device(ipfd, ifr.ifr_name, ip, conf.mac);

	/* Tell Guest what MAC address to use. */
	add_feature(dev, VIRTIO_NET_F_MAC);
	set_config(dev, sizeof(conf), &conf);

	/* We don't need the socket any more; setup is done. */
	close(ipfd);

	verbose("device %u: tun net %u.%u.%u.%u\n",
		devices.device_num++,
		(u8)(ip>>24),(u8)(ip>>16),(u8)(ip>>8),(u8)ip);
	if (br_name)
		verbose("attached to bridge: %s\n", br_name);
}

/* Our block (disk) device should be really simple: the Guest asks for a block
 * number and we read or write that position in the file.  Unfortunately, that
 * was amazingly slow: the Guest waits until the read is finished before
 * running anything else, even if it could have been doing useful work.
 *
 * We could use async I/O, except it's reputed to suck so hard that characters
 * actually go missing from your code when you try to use it.
 *
 * So we farm the I/O out to thread, and communicate with it via a pipe. */

/* This hangs off device->priv. */
struct vblk_info
{
	/* The size of the file. */
	off64_t len;

	/* The file descriptor for the file. */
	int fd;

	/* IO thread listens on this file descriptor [0]. */
	int workpipe[2];

	/* IO thread writes to this file descriptor to mark it done, then
	 * Launcher triggers interrupt to Guest. */
	int done_fd;
};

/*L:210
 * The Disk
 *
 * Remember that the block device is handled by a separate I/O thread.  We head
 * straight into the core of that thread here:
 */
static bool service_io(struct device *dev)
{
	struct vblk_info *vblk = dev->priv;
	unsigned int head, out_num, in_num, wlen;
	int ret;
	struct virtio_blk_inhdr *in;
	struct virtio_blk_outhdr *out;
	struct iovec iov[dev->vq->vring.num];
	off64_t off;

	/* See if there's a request waiting.  If not, nothing to do. */
	head = get_vq_desc(dev->vq, iov, &out_num, &in_num);
	if (head == dev->vq->vring.num)
		return false;

	/* Every block request should contain at least one output buffer
	 * (detailing the location on disk and the type of request) and one
	 * input buffer (to hold the result). */
	if (out_num == 0 || in_num == 0)
		errx(1, "Bad virtblk cmd %u out=%u in=%u",
		     head, out_num, in_num);

	out = convert(&iov[0], struct virtio_blk_outhdr);
	in = convert(&iov[out_num+in_num-1], struct virtio_blk_inhdr);
	off = out->sector * 512;

	/* The block device implements "barriers", where the Guest indicates
	 * that it wants all previous writes to occur before this write.  We
	 * don't have a way of asking our kernel to do a barrier, so we just
	 * synchronize all the data in the file.  Pretty poor, no? */
	if (out->type & VIRTIO_BLK_T_BARRIER)
		fdatasync(vblk->fd);

	/* In general the virtio block driver is allowed to try SCSI commands.
	 * It'd be nice if we supported eject, for example, but we don't. */
	if (out->type & VIRTIO_BLK_T_SCSI_CMD) {
		fprintf(stderr, "Scsi commands unsupported\n");
		in->status = VIRTIO_BLK_S_UNSUPP;
		wlen = sizeof(*in);
	} else if (out->type & VIRTIO_BLK_T_OUT) {
		/* Write */

		/* Move to the right location in the block file.  This can fail
		 * if they try to write past end. */
		if (lseek64(vblk->fd, off, SEEK_SET) != off)
			err(1, "Bad seek to sector %llu", out->sector);

		ret = writev(vblk->fd, iov+1, out_num-1);
		verbose("WRITE to sector %llu: %i\n", out->sector, ret);

		/* Grr... Now we know how long the descriptor they sent was, we
		 * make sure they didn't try to write over the end of the block
		 * file (possibly extending it). */
		if (ret > 0 && off + ret > vblk->len) {
			/* Trim it back to the correct length */
			ftruncate64(vblk->fd, vblk->len);
			/* Die, bad Guest, die. */
			errx(1, "Write past end %llu+%u", off, ret);
		}
		wlen = sizeof(*in);
		in->status = (ret >= 0 ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR);
	} else {
		/* Read */

		/* Move to the right location in the block file.  This can fail
		 * if they try to read past end. */
		if (lseek64(vblk->fd, off, SEEK_SET) != off)
			err(1, "Bad seek to sector %llu", out->sector);

		ret = readv(vblk->fd, iov+1, in_num-1);
		verbose("READ from sector %llu: %i\n", out->sector, ret);
		if (ret >= 0) {
			wlen = sizeof(*in) + ret;
			in->status = VIRTIO_BLK_S_OK;
		} else {
			wlen = sizeof(*in);
			in->status = VIRTIO_BLK_S_IOERR;
		}
	}

	/* We can't trigger an IRQ, because we're not the Launcher.  It does
	 * that when we tell it we're done. */
	add_used(dev->vq, head, wlen);
	return true;
}

/* This is the thread which actually services the I/O. */
static int io_thread(void *_dev)
{
	struct device *dev = _dev;
	struct vblk_info *vblk = dev->priv;
	char c;

	/* Close other side of workpipe so we get 0 read when main dies. */
	close(vblk->workpipe[1]);
	/* Close the other side of the done_fd pipe. */
	close(dev->fd);

	/* When this read fails, it means Launcher died, so we follow. */
	while (read(vblk->workpipe[0], &c, 1) == 1) {
		/* We acknowledge each request immediately to reduce latency,
		 * rather than waiting until we've done them all.  I haven't
		 * measured to see if it makes any difference.
		 *
		 * That would be an interesting test, wouldn't it?  You could
		 * also try having more than one I/O thread. */
		while (service_io(dev))
			write(vblk->done_fd, &c, 1);
	}
	return 0;
}

/* Now we've seen the I/O thread, we return to the Launcher to see what happens
 * when that thread tells us it's completed some I/O. */
static bool handle_io_finish(int fd, struct device *dev)
{
	char c;

	/* If the I/O thread died, presumably it printed the error, so we
	 * simply exit. */
	if (read(dev->fd, &c, 1) != 1)
		exit(1);

	/* It did some work, so trigger the irq. */
	trigger_irq(fd, dev->vq);
	return true;
}

/* When the Guest submits some I/O, we just need to wake the I/O thread. */
static void handle_virtblk_output(int fd, struct virtqueue *vq)
{
	struct vblk_info *vblk = vq->dev->priv;
	char c = 0;

	/* Wake up I/O thread and tell it to go to work! */
	if (write(vblk->workpipe[1], &c, 1) != 1)
		/* Presumably it indicated why it died. */
		exit(1);
}

/*L:198 This actually sets up a virtual block device. */
static void setup_block_file(const char *filename)
{
	int p[2];
	struct device *dev;
	struct vblk_info *vblk;
	void *stack;
	struct virtio_blk_config conf;

	/* This is the pipe the I/O thread will use to tell us I/O is done. */
	pipe(p);

	/* The device responds to return from I/O thread. */
	dev = new_device("block", VIRTIO_ID_BLOCK, p[0], handle_io_finish);

	/* The device has one virtqueue, where the Guest places requests. */
	add_virtqueue(dev, VIRTQUEUE_NUM, handle_virtblk_output);

	/* Allocate the room for our own bookkeeping */
	vblk = dev->priv = malloc(sizeof(*vblk));

	/* First we open the file and store the length. */
	vblk->fd = open_or_die(filename, O_RDWR|O_LARGEFILE);
	vblk->len = lseek64(vblk->fd, 0, SEEK_END);

	/* We support barriers. */
	add_feature(dev, VIRTIO_BLK_F_BARRIER);

	/* Tell Guest how many sectors this device has. */
	conf.capacity = cpu_to_le64(vblk->len / 512);

	/* Tell Guest not to put in too many descriptors at once: two are used
	 * for the in and out elements. */
	add_feature(dev, VIRTIO_BLK_F_SEG_MAX);
	conf.seg_max = cpu_to_le32(VIRTQUEUE_NUM - 2);

	set_config(dev, sizeof(conf), &conf);

	/* The I/O thread writes to this end of the pipe when done. */
	vblk->done_fd = p[1];

	/* This is the second pipe, which is how we tell the I/O thread about
	 * more work. */
	pipe(vblk->workpipe);

	/* Create stack for thread and run it.  Since stack grows upwards, we
	 * point the stack pointer to the end of this region. */
	stack = malloc(32768);
	/* SIGCHLD - We dont "wait" for our cloned thread, so prevent it from
	 * becoming a zombie. */
	if (clone(io_thread, stack + 32768, CLONE_VM | SIGCHLD, dev) == -1)
		err(1, "Creating clone");

	/* We don't need to keep the I/O thread's end of the pipes open. */
	close(vblk->done_fd);
	close(vblk->workpipe[0]);

	verbose("device %u: virtblock %llu sectors\n",
		devices.device_num, le64_to_cpu(conf.capacity));
}
/* That's the end of device setup. */

/*L:230 Reboot is pretty easy: clean up and exec() the Launcher afresh. */
static void __attribute__((noreturn)) restart_guest(void)
{
	unsigned int i;

	/* Closing pipes causes the Waker thread and io_threads to die, and
	 * closing /dev/lguest cleans up the Guest.  Since we don't track all
	 * open fds, we simply close everything beyond stderr. */
	for (i = 3; i < FD_SETSIZE; i++)
		close(i);
	execv(main_args[0], main_args);
	err(1, "Could not exec %s", main_args[0]);
}

/*L:220 Finally we reach the core of the Launcher which runs the Guest, serves
 * its input and output, and finally, lays it to rest. */
static void __attribute__((noreturn)) run_guest(int lguest_fd)
{
	for (;;) {
		unsigned long args[] = { LHREQ_BREAK, 0 };
		unsigned long notify_addr;
		int readval;

		/* We read from the /dev/lguest device to run the Guest. */
		readval = pread(lguest_fd, &notify_addr,
				sizeof(notify_addr), cpu_id);

		/* One unsigned long means the Guest did HCALL_NOTIFY */
		if (readval == sizeof(notify_addr)) {
			verbose("Notify on address %#lx\n", notify_addr);
			handle_output(lguest_fd, notify_addr);
			continue;
		/* ENOENT means the Guest died.  Reading tells us why. */
		} else if (errno == ENOENT) {
			char reason[1024] = { 0 };
			pread(lguest_fd, reason, sizeof(reason)-1, cpu_id);
			errx(1, "%s", reason);
		/* ERESTART means that we need to reboot the guest */
		} else if (errno == ERESTART) {
			restart_guest();
		/* EAGAIN means the Waker wanted us to look at some input.
		 * Anything else means a bug or incompatible change. */
		} else if (errno != EAGAIN)
			err(1, "Running guest failed");

		/* Only service input on thread for CPU 0. */
		if (cpu_id != 0)
			continue;

		/* Service input, then unset the BREAK to release the Waker. */
		handle_input(lguest_fd);
		if (pwrite(lguest_fd, args, sizeof(args), cpu_id) < 0)
			err(1, "Resetting break");
	}
}
/*L:240
 * This is the end of the Launcher.  The good news: we are over halfway
 * through!  The bad news: the most fiendish part of the code still lies ahead
 * of us.
 *
 * Are you ready?  Take a deep breath and join me in the core of the Host, in
 * "make Host".
 :*/

static struct option opts[] = {
	{ "verbose", 0, NULL, 'v' },
	{ "tunnet", 1, NULL, 't' },
	{ "block", 1, NULL, 'b' },
	{ "initrd", 1, NULL, 'i' },
	{ NULL },
};
static void usage(void)
{
	errx(1, "Usage: lguest [--verbose] "
	     "[--tunnet=(<ipaddr>|bridge:<bridgename>)\n"
	     "|--block=<filename>|--initrd=<filename>]...\n"
	     "<mem-in-mb> vmlinux [args...]");
}

/*L:105 The main routine is where the real work begins: */
int main(int argc, char *argv[])
{
	/* Memory, top-level pagetable, code startpoint and size of the
	 * (optional) initrd. */
	unsigned long mem = 0, pgdir, start, initrd_size = 0;
	/* Two temporaries and the /dev/lguest file descriptor. */
	int i, c, lguest_fd;
	/* The boot information for the Guest. */
	struct boot_params *boot;
	/* If they specify an initrd file to load. */
	const char *initrd_name = NULL;

	/* Save the args: we "reboot" by execing ourselves again. */
	main_args = argv;
	/* We don't "wait" for the children, so prevent them from becoming
	 * zombies. */
	signal(SIGCHLD, SIG_IGN);

	/* First we initialize the device list.  Since console and network
	 * device receive input from a file descriptor, we keep an fdset
	 * (infds) and the maximum fd number (max_infd) with the head of the
	 * list.  We also keep a pointer to the last device.  Finally, we keep
	 * the next interrupt number to use for devices (1: remember that 0 is
	 * used by the timer). */
	FD_ZERO(&devices.infds);
	devices.max_infd = -1;
	devices.lastdev = NULL;
	devices.next_irq = 1;

	cpu_id = 0;
	/* We need to know how much memory so we can set up the device
	 * descriptor and memory pages for the devices as we parse the command
	 * line.  So we quickly look through the arguments to find the amount
	 * of memory now. */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			mem = atoi(argv[i]) * 1024 * 1024;
			/* We start by mapping anonymous pages over all of
			 * guest-physical memory range.  This fills it with 0,
			 * and ensures that the Guest won't be killed when it
			 * tries to access it. */
			guest_base = map_zeroed_pages(mem / getpagesize()
						      + DEVICE_PAGES);
			guest_limit = mem;
			guest_max = mem + DEVICE_PAGES*getpagesize();
			devices.descpage = get_pages(1);
			break;
		}
	}

	/* The options are fairly straight-forward */
	while ((c = getopt_long(argc, argv, "v", opts, NULL)) != EOF) {
		switch (c) {
		case 'v':
			verbose = true;
			break;
		case 't':
			setup_tun_net(optarg);
			break;
		case 'b':
			setup_block_file(optarg);
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

	verbose("Guest base is at %p\n", guest_base);

	/* We always have a console device */
	setup_console();

	/* Now we load the kernel */
	start = load_kernel(open_or_die(argv[optind+1], O_RDONLY));

	/* Boot information is stashed at physical address 0 */
	boot = from_guest_phys(0);

	/* Map the initrd image if requested (at top of physical memory) */
	if (initrd_name) {
		initrd_size = load_initrd(initrd_name, mem);
		/* These are the location in the Linux boot header where the
		 * start and size of the initrd are expected to be found. */
		boot->hdr.ramdisk_image = mem - initrd_size;
		boot->hdr.ramdisk_size = initrd_size;
		/* The bootloader type 0xFF means "unknown"; that's OK. */
		boot->hdr.type_of_loader = 0xFF;
	}

	/* Set up the initial linear pagetables, starting below the initrd. */
	pgdir = setup_pagetables(mem, initrd_size);

	/* The Linux boot header contains an "E820" memory map: ours is a
	 * simple, single region. */
	boot->e820_entries = 1;
	boot->e820_map[0] = ((struct e820entry) { 0, mem, E820_RAM });
	/* The boot header contains a command line pointer: we put the command
	 * line after the boot header. */
	boot->hdr.cmd_line_ptr = to_guest_phys(boot + 1);
	/* We use a simple helper to copy the arguments separated by spaces. */
	concat((char *)(boot + 1), argv+optind+2);

	/* Boot protocol version: 2.07 supports the fields for lguest. */
	boot->hdr.version = 0x207;

	/* The hardware_subarch value of "1" tells the Guest it's an lguest. */
	boot->hdr.hardware_subarch = 1;

	/* Tell the entry path not to try to reload segment registers. */
	boot->hdr.loadflags |= KEEP_SEGMENTS;

	/* We tell the kernel to initialize the Guest: this returns the open
	 * /dev/lguest file descriptor. */
	lguest_fd = tell_kernel(pgdir, start);

	/* We fork off a child process, which wakes the Launcher whenever one
	 * of the input file descriptors needs attention.  We call this the
	 * Waker, and we'll cover it in a moment. */
	waker_fd = setup_waker(lguest_fd);

	/* Finally, run the Guest.  This doesn't return. */
	run_guest(lguest_fd);
}
/*:*/

/*M:999
 * Mastery is done: you now know everything I do.
 *
 * But surely you have seen code, features and bugs in your wanderings which
 * you now yearn to attack?  That is the real game, and I look forward to you
 * patching and forking lguest into the Your-Name-Here-visor.
 *
 * Farewell, and good coding!
 * Rusty Russell.
 */
