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
typedef unsigned long long u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#include "../../include/linux/lguest_launcher.h"
#include "../../include/asm-i386/e820.h"

#define PAGE_PRESENT 0x7 	/* Present, RW, Execute */
#define NET_PEERNUM 1
#define BRIDGE_PFX "bridge:"
#ifndef SIOCBRADDIF
#define SIOCBRADDIF	0x89a2		/* add interface to bridge      */
#endif

static bool verbose;
#define verbose(args...) \
	do { if (verbose) printf(args); } while(0)
static int waker_fd;
static u32 top;

struct device_list
{
	fd_set infds;
	int max_infd;

	struct lguest_device_desc *descs;
	struct device *dev;
	struct device **lastdev;
};

struct device
{
	struct device *next;
	struct lguest_device_desc *desc;
	void *mem;

	/* Watch this fd if handle_input non-NULL. */
	int fd;
	bool (*handle_input)(int fd, struct device *me);

	/* Watch DMA to this key if handle_input non-NULL. */
	unsigned long watch_key;
	u32 (*handle_output)(int fd, const struct iovec *iov,
			     unsigned int num, struct device *me);

	/* Device-specific data. */
	void *priv;
};

static int open_or_die(const char *name, int flags)
{
	int fd = open(name, flags);
	if (fd < 0)
		err(1, "Failed to open %s", name);
	return fd;
}

static void *map_zeroed_pages(unsigned long addr, unsigned int num)
{
	static int fd = -1;

	if (fd == -1)
		fd = open_or_die("/dev/zero", O_RDONLY);

	if (mmap((void *)addr, getpagesize() * num,
		 PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_PRIVATE, fd, 0)
	    != (void *)addr)
		err(1, "Mmaping %u pages of /dev/zero @%p", num, (void *)addr);
	return (void *)addr;
}

/* Find magic string marking entry point, return entry point. */
static unsigned long entry_point(void *start, void *end,
				 unsigned long page_offset)
{
	void *p;

	for (p = start; p < end; p++)
		if (memcmp(p, "GenuineLguest", strlen("GenuineLguest")) == 0)
			return (long)p + strlen("GenuineLguest") + page_offset;

	err(1, "Is this image a genuine lguest?");
}

/* Returns the entry point */
static unsigned long map_elf(int elf_fd, const Elf32_Ehdr *ehdr,
			     unsigned long *page_offset)
{
	void *addr;
	Elf32_Phdr phdr[ehdr->e_phnum];
	unsigned int i;
	unsigned long start = -1UL, end = 0;

	/* Sanity checks. */
	if (ehdr->e_type != ET_EXEC
	    || ehdr->e_machine != EM_386
	    || ehdr->e_phentsize != sizeof(Elf32_Phdr)
	    || ehdr->e_phnum < 1 || ehdr->e_phnum > 65536U/sizeof(Elf32_Phdr))
		errx(1, "Malformed elf header");

	if (lseek(elf_fd, ehdr->e_phoff, SEEK_SET) < 0)
		err(1, "Seeking to program headers");
	if (read(elf_fd, phdr, sizeof(phdr)) != sizeof(phdr))
		err(1, "Reading program headers");

	*page_offset = 0;
	/* We map the loadable segments at virtual addresses corresponding
	 * to their physical addresses (our virtual == guest physical). */
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD)
			continue;

		verbose("Section %i: size %i addr %p\n",
			i, phdr[i].p_memsz, (void *)phdr[i].p_paddr);

		/* We expect linear address space. */
		if (!*page_offset)
			*page_offset = phdr[i].p_vaddr - phdr[i].p_paddr;
		else if (*page_offset != phdr[i].p_vaddr - phdr[i].p_paddr)
			errx(1, "Page offset of section %i different", i);

		if (phdr[i].p_paddr < start)
			start = phdr[i].p_paddr;
		if (phdr[i].p_paddr + phdr[i].p_filesz > end)
			end = phdr[i].p_paddr + phdr[i].p_filesz;

		/* We map everything private, writable. */
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

/* This is amazingly reliable. */
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

static unsigned long unpack_bzimage(int fd, unsigned long *page_offset)
{
	gzFile f;
	int ret, len = 0;
	void *img = (void *)0x100000;

	f = gzdopen(fd, "rb");
	while ((ret = gzread(f, img + len, 65536)) > 0)
		len += ret;
	if (ret < 0)
		err(1, "reading image from bzImage");

	verbose("Unpacked size %i addr %p\n", len, img);
	*page_offset = intuit_page_offset(img, len);

	return entry_point(img, img + len, *page_offset);
}

static unsigned long load_bzimage(int fd, unsigned long *page_offset)
{
	unsigned char c;
	int state = 0;

	/* Ugly brute force search for gzip header. */
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
			lseek(fd, -10, SEEK_CUR);
			if (c != 0x03) /* Compressed under UNIX. */
				state = -1;
			else
				return unpack_bzimage(fd, page_offset);
		}
	}
	errx(1, "Could not find kernel in bzImage");
}

static unsigned long load_kernel(int fd, unsigned long *page_offset)
{
	Elf32_Ehdr hdr;

	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		err(1, "Reading kernel");

	if (memcmp(hdr.e_ident, ELFMAG, SELFMAG) == 0)
		return map_elf(fd, &hdr, page_offset);

	return load_bzimage(fd, page_offset);
}

static inline unsigned long page_align(unsigned long addr)
{
	return ((addr + getpagesize()-1) & ~(getpagesize()-1));
}

/* initrd gets loaded at top of memory: return length. */
static unsigned long load_initrd(const char *name, unsigned long mem)
{
	int ifd;
	struct stat st;
	unsigned long len;
	void *iaddr;

	ifd = open_or_die(name, O_RDONLY);
	if (fstat(ifd, &st) < 0)
		err(1, "fstat() on initrd '%s'", name);

	len = page_align(st.st_size);
	iaddr = mmap((void *)mem - len, st.st_size,
		     PROT_READ|PROT_EXEC|PROT_WRITE,
		     MAP_FIXED|MAP_PRIVATE, ifd, 0);
	if (iaddr != (void *)mem - len)
		err(1, "Mmaping initrd '%s' returned %p not %p",
		    name, iaddr, (void *)mem - len);
	close(ifd);
	verbose("mapped initrd %s size=%lu @ %p\n", name, st.st_size, iaddr);
	return len;
}

static unsigned long setup_pagetables(unsigned long mem,
				      unsigned long initrd_size,
				      unsigned long page_offset)
{
	u32 *pgdir, *linear;
	unsigned int mapped_pages, i, linear_pages;
	unsigned int ptes_per_page = getpagesize()/sizeof(u32);

	/* If we can map all of memory above page_offset, we do so. */
	if (mem <= -page_offset)
		mapped_pages = mem/getpagesize();
	else
		mapped_pages = -page_offset/getpagesize();

	/* Each linear PTE page can map ptes_per_page pages. */
	linear_pages = (mapped_pages + ptes_per_page-1)/ptes_per_page;

	/* We lay out top-level then linear mapping immediately below initrd */
	pgdir = (void *)mem - initrd_size - getpagesize();
	linear = (void *)pgdir - linear_pages*getpagesize();

	for (i = 0; i < mapped_pages; i++)
		linear[i] = ((i * getpagesize()) | PAGE_PRESENT);

	/* Now set up pgd so that this memory is at page_offset */
	for (i = 0; i < mapped_pages; i += ptes_per_page) {
		pgdir[(i + page_offset/getpagesize())/ptes_per_page]
			= (((u32)linear + i*sizeof(u32)) | PAGE_PRESENT);
	}

	verbose("Linear mapping of %u pages in %u pte pages at %p\n",
		mapped_pages, linear_pages, linear);

	return (unsigned long)pgdir;
}

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

static int tell_kernel(u32 pgdir, u32 start, u32 page_offset)
{
	u32 args[] = { LHREQ_INITIALIZE,
		       top/getpagesize(), pgdir, start, page_offset };
	int fd;

	fd = open_or_die("/dev/lguest", O_RDWR);
	if (write(fd, args, sizeof(args)) < 0)
		err(1, "Writing to /dev/lguest");
	return fd;
}

static void set_fd(int fd, struct device_list *devices)
{
	FD_SET(fd, &devices->infds);
	if (fd > devices->max_infd)
		devices->max_infd = fd;
}

/* When input arrives, we tell the kernel to kick lguest out with -EAGAIN. */
static void wake_parent(int pipefd, int lguest_fd, struct device_list *devices)
{
	set_fd(pipefd, devices);

	for (;;) {
		fd_set rfds = devices->infds;
		u32 args[] = { LHREQ_BREAK, 1 };

		select(devices->max_infd+1, &rfds, NULL, NULL, NULL);
		if (FD_ISSET(pipefd, &rfds)) {
			int ignorefd;
			if (read(pipefd, &ignorefd, sizeof(ignorefd)) == 0)
				exit(0);
			FD_CLR(ignorefd, &devices->infds);
		} else
			write(lguest_fd, args, sizeof(args));
	}
}

static int setup_waker(int lguest_fd, struct device_list *device_list)
{
	int pipefd[2], child;

	pipe(pipefd);
	child = fork();
	if (child == -1)
		err(1, "forking");

	if (child == 0) {
		close(pipefd[1]);
		wake_parent(pipefd[0], lguest_fd, device_list);
	}
	close(pipefd[0]);

	return pipefd[1];
}

static void *_check_pointer(unsigned long addr, unsigned int size,
			    unsigned int line)
{
	if (addr >= top || addr + size >= top)
		errx(1, "%s:%i: Invalid address %li", __FILE__, line, addr);
	return (void *)addr;
}
#define check_pointer(addr,size) _check_pointer(addr, size, __LINE__)

/* Returns pointer to dma->used_len */
static u32 *dma2iov(unsigned long dma, struct iovec iov[], unsigned *num)
{
	unsigned int i;
	struct lguest_dma *udma;

	udma = check_pointer(dma, sizeof(*udma));
	for (i = 0; i < LGUEST_MAX_DMA_SECTIONS; i++) {
		if (!udma->len[i])
			break;

		iov[i].iov_base = check_pointer(udma->addr[i], udma->len[i]);
		iov[i].iov_len = udma->len[i];
	}
	*num = i;
	return &udma->used_len;
}

static u32 *get_dma_buffer(int fd, void *key,
			   struct iovec iov[], unsigned int *num, u32 *irq)
{
	u32 buf[] = { LHREQ_GETDMA, (u32)key };
	unsigned long udma;
	u32 *res;

	udma = write(fd, buf, sizeof(buf));
	if (udma == (unsigned long)-1)
		return NULL;

	/* Kernel stashes irq in ->used_len. */
	res = dma2iov(udma, iov, num);
	*irq = *res;
	return res;
}

static void trigger_irq(int fd, u32 irq)
{
	u32 buf[] = { LHREQ_IRQ, irq };
	if (write(fd, buf, sizeof(buf)) != 0)
		err(1, "Triggering irq %i", irq);
}

static void discard_iovec(struct iovec *iov, unsigned int *num)
{
	static char discard_buf[1024];
	*num = 1;
	iov->iov_base = discard_buf;
	iov->iov_len = sizeof(discard_buf);
}

static struct termios orig_term;
static void restore_term(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
}

struct console_abort
{
	int count;
	struct timeval start;
};

/* We DMA input to buffer bound at start of console page. */
static bool handle_console_input(int fd, struct device *dev)
{
	u32 irq = 0, *lenp;
	int len;
	unsigned int num;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];
	struct console_abort *abort = dev->priv;

	lenp = get_dma_buffer(fd, dev->mem, iov, &num, &irq);
	if (!lenp) {
		warn("console: no dma buffer!");
		discard_iovec(iov, &num);
	}

	len = readv(dev->fd, iov, num);
	if (len <= 0) {
		warnx("Failed to get console input, ignoring console.");
		len = 0;
	}

	if (lenp) {
		*lenp = len;
		trigger_irq(fd, irq);
	}

	/* Three ^C within one second?  Exit. */
	if (len == 1 && ((char *)iov[0].iov_base)[0] == 3) {
		if (!abort->count++)
			gettimeofday(&abort->start, NULL);
		else if (abort->count == 3) {
			struct timeval now;
			gettimeofday(&now, NULL);
			if (now.tv_sec <= abort->start.tv_sec+1) {
				/* Make sure waker is not blocked in BREAK */
				u32 args[] = { LHREQ_BREAK, 0 };
				close(waker_fd);
				write(fd, args, sizeof(args));
				exit(2);
			}
			abort->count = 0;
		}
	} else
		abort->count = 0;

	if (!len) {
		restore_term();
		return false;
	}
	return true;
}

static u32 handle_console_output(int fd, const struct iovec *iov,
				 unsigned num, struct device*dev)
{
	return writev(STDOUT_FILENO, iov, num);
}

static u32 handle_tun_output(int fd, const struct iovec *iov,
			     unsigned num, struct device *dev)
{
	/* Now we've seen output, we should warn if we can't get buffers. */
	*(bool *)dev->priv = true;
	return writev(dev->fd, iov, num);
}

static unsigned long peer_offset(unsigned int peernum)
{
	return 4 * peernum;
}

static bool handle_tun_input(int fd, struct device *dev)
{
	u32 irq = 0, *lenp;
	int len;
	unsigned num;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];

	lenp = get_dma_buffer(fd, dev->mem+peer_offset(NET_PEERNUM), iov, &num,
			      &irq);
	if (!lenp) {
		if (*(bool *)dev->priv)
			warn("network: no dma buffer!");
		discard_iovec(iov, &num);
	}

	len = readv(dev->fd, iov, num);
	if (len <= 0)
		err(1, "reading network");
	if (lenp) {
		*lenp = len;
		trigger_irq(fd, irq);
	}
	verbose("tun input packet len %i [%02x %02x] (%s)\n", len,
		((u8 *)iov[0].iov_base)[0], ((u8 *)iov[0].iov_base)[1],
		lenp ? "sent" : "discarded");
	return true;
}

static u32 handle_block_output(int fd, const struct iovec *iov,
			       unsigned num, struct device *dev)
{
	struct lguest_block_page *p = dev->mem;
	u32 irq, *lenp;
	unsigned int len, reply_num;
	struct iovec reply[LGUEST_MAX_DMA_SECTIONS];
	off64_t device_len, off = (off64_t)p->sector * 512;

	device_len = *(off64_t *)dev->priv;

	if (off >= device_len)
		err(1, "Bad offset %llu vs %llu", off, device_len);
	if (lseek64(dev->fd, off, SEEK_SET) != off)
		err(1, "Bad seek to sector %i", p->sector);

	verbose("Block: %s at offset %llu\n", p->type ? "WRITE" : "READ", off);

	lenp = get_dma_buffer(fd, dev->mem, reply, &reply_num, &irq);
	if (!lenp)
		err(1, "Block request didn't give us a dma buffer");

	if (p->type) {
		len = writev(dev->fd, iov, num);
		if (off + len > device_len) {
			ftruncate(dev->fd, device_len);
			errx(1, "Write past end %llu+%u", off, len);
		}
		*lenp = 0;
	} else {
		len = readv(dev->fd, reply, reply_num);
		*lenp = len;
	}

	p->result = 1 + (p->bytes != len);
	trigger_irq(fd, irq);
	return 0;
}

static void handle_output(int fd, unsigned long dma, unsigned long key,
			  struct device_list *devices)
{
	struct device *i;
	u32 *lenp;
	struct iovec iov[LGUEST_MAX_DMA_SECTIONS];
	unsigned num = 0;

	lenp = dma2iov(dma, iov, &num);
	for (i = devices->dev; i; i = i->next) {
		if (i->handle_output && key == i->watch_key) {
			*lenp = i->handle_output(fd, iov, num, i);
			return;
		}
	}
	warnx("Pending dma %p, key %p", (void *)dma, (void *)key);
}

static void handle_input(int fd, struct device_list *devices)
{
	struct timeval poll = { .tv_sec = 0, .tv_usec = 0 };

	for (;;) {
		struct device *i;
		fd_set fds = devices->infds;

		if (select(devices->max_infd+1, &fds, NULL, NULL, &poll) == 0)
			break;

		for (i = devices->dev; i; i = i->next) {
			if (i->handle_input && FD_ISSET(i->fd, &fds)) {
				if (!i->handle_input(fd, i)) {
					FD_CLR(i->fd, &devices->infds);
					/* Tell waker to ignore it too... */
					write(waker_fd, &i->fd, sizeof(i->fd));
				}
			}
		}
	}
}

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

	/* Append to device list. */
	*devices->lastdev = dev;
	dev->next = NULL;
	devices->lastdev = &dev->next;

	dev->fd = fd;
	if (handle_input)
		set_fd(dev->fd, devices);
	dev->desc = new_dev_desc(devices->descs, type, features, num_pages);
	dev->mem = (void *)(dev->desc->pfn * getpagesize());
	dev->handle_input = handle_input;
	dev->watch_key = (unsigned long)dev->mem + watch_off;
	dev->handle_output = handle_output;
	return dev;
}

static void setup_console(struct device_list *devices)
{
	struct device *dev;

	if (tcgetattr(STDIN_FILENO, &orig_term) == 0) {
		struct termios term = orig_term;
		term.c_lflag &= ~(ISIG|ICANON|ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
		atexit(restore_term);
	}

	/* We don't currently require a page for the console. */
	dev = new_device(devices, LGUEST_DEVICE_T_CONSOLE, 0, 0,
			 STDIN_FILENO, handle_console_input,
			 LGUEST_CONSOLE_DMA_KEY, handle_console_output);
	dev->priv = malloc(sizeof(struct console_abort));
	((struct console_abort *)dev->priv)->count = 0;
	verbose("device %p: console\n",
		(void *)(dev->desc->pfn * getpagesize()));
}

static void setup_block_file(const char *filename, struct device_list *devices)
{
	int fd;
	struct device *dev;
	off64_t *device_len;
	struct lguest_block_page *p;

	fd = open_or_die(filename, O_RDWR|O_LARGEFILE|O_DIRECT);
	dev = new_device(devices, LGUEST_DEVICE_T_BLOCK, 1,
			 LGUEST_DEVICE_F_RANDOMNESS,
			 fd, NULL, 0, handle_block_output);
	device_len = dev->priv = malloc(sizeof(*device_len));
	*device_len = lseek64(fd, 0, SEEK_END);
	p = dev->mem;

	p->num_sectors = *device_len/512;
	verbose("device %p: block %i sectors\n",
		(void *)(dev->desc->pfn * getpagesize()), p->num_sectors);
}

/* We use fnctl locks to reserve network slots (autocleanup!) */
static unsigned int find_slot(int netfd, const char *filename)
{
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_len = 1;
	for (fl.l_start = 0;
	     fl.l_start < getpagesize()/sizeof(struct lguest_net);
	     fl.l_start++) {
		if (fcntl(netfd, F_SETLK, &fl) == 0)
			return fl.l_start;
	}
	errx(1, "No free slots in network file %s", filename);
}

static void setup_net_file(const char *filename,
			   struct device_list *devices)
{
	int netfd;
	struct device *dev;

	netfd = open(filename, O_RDWR, 0);
	if (netfd < 0) {
		if (errno == ENOENT) {
			netfd = open(filename, O_RDWR|O_CREAT, 0600);
			if (netfd >= 0) {
				char page[getpagesize()];
				memset(page, 0, sizeof(page));
				write(netfd, page, sizeof(page));
			}
		}
		if (netfd < 0)
			err(1, "cannot open net file '%s'", filename);
	}

	dev = new_device(devices, LGUEST_DEVICE_T_NET, 1,
			 find_slot(netfd, filename)|LGUEST_NET_F_NOCSUM,
			 -1, NULL, 0, NULL);

	/* We overwrite the /dev/zero mapping with the actual file. */
	if (mmap(dev->mem, getpagesize(), PROT_READ|PROT_WRITE,
			 MAP_FIXED|MAP_SHARED, netfd, 0) != dev->mem)
			err(1, "could not mmap '%s'", filename);
	verbose("device %p: shared net %s, peer %i\n",
		(void *)(dev->desc->pfn * getpagesize()), filename,
		dev->desc->features & ~LGUEST_NET_F_NOCSUM);
}

static u32 str2ip(const char *ipaddr)
{
	unsigned int byte[4];

	sscanf(ipaddr, "%u.%u.%u.%u", &byte[0], &byte[1], &byte[2], &byte[3]);
	return (byte[0] << 24) | (byte[1] << 16) | (byte[2] << 8) | byte[3];
}

/* adapted from libbridge */
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

static void configure_device(int fd, const char *devname, u32 ipaddr,
			     unsigned char hwaddr[6])
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, devname);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(ipaddr);
	if (ioctl(fd, SIOCSIFADDR, &ifr) != 0)
		err(1, "Setting %s interface address", devname);
	ifr.ifr_flags = IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		err(1, "Bringing interface %s up", devname);

	if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0)
		err(1, "getting hw address for %s", devname);

	memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);
}

static void setup_tun_net(const char *arg, struct device_list *devices)
{
	struct device *dev;
	struct ifreq ifr;
	int netfd, ipfd;
	u32 ip;
	const char *br_name = NULL;

	netfd = open_or_die("/dev/net/tun", O_RDWR);
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strcpy(ifr.ifr_name, "tap%d");
	if (ioctl(netfd, TUNSETIFF, &ifr) != 0)
		err(1, "configuring /dev/net/tun");
	ioctl(netfd, TUNSETNOCSUM, 1);

	/* You will be peer 1: we should create enough jitter to randomize */
	dev = new_device(devices, LGUEST_DEVICE_T_NET, 1,
			 NET_PEERNUM|LGUEST_DEVICE_F_RANDOMNESS, netfd,
			 handle_tun_input, peer_offset(0), handle_tun_output);
	dev->priv = malloc(sizeof(bool));
	*(bool *)dev->priv = false;

	ipfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (ipfd < 0)
		err(1, "opening IP socket");

	if (!strncmp(BRIDGE_PFX, arg, strlen(BRIDGE_PFX))) {
		ip = INADDR_ANY;
		br_name = arg + strlen(BRIDGE_PFX);
		add_to_bridge(ipfd, ifr.ifr_name, br_name);
	} else
		ip = str2ip(arg);

	/* We are peer 0, ie. first slot. */
	configure_device(ipfd, ifr.ifr_name, ip, dev->mem);

	/* Set "promisc" bit: we want every single packet. */
	*((u8 *)dev->mem) |= 0x1;

	close(ipfd);

	verbose("device %p: tun net %u.%u.%u.%u\n",
		(void *)(dev->desc->pfn * getpagesize()),
		(u8)(ip>>24), (u8)(ip>>16), (u8)(ip>>8), (u8)ip);
	if (br_name)
		verbose("attached to bridge: %s\n", br_name);
}

static void __attribute__((noreturn))
run_guest(int lguest_fd, struct device_list *device_list)
{
	for (;;) {
		u32 args[] = { LHREQ_BREAK, 0 };
		unsigned long arr[2];
		int readval;

		/* We read from the /dev/lguest device to run the Guest. */
		readval = read(lguest_fd, arr, sizeof(arr));

		if (readval == sizeof(arr)) {
			handle_output(lguest_fd, arr[0], arr[1], device_list);
			continue;
		} else if (errno == ENOENT) {
			char reason[1024] = { 0 };
			read(lguest_fd, reason, sizeof(reason)-1);
			errx(1, "%s", reason);
		} else if (errno != EAGAIN)
			err(1, "Running guest failed");
		handle_input(lguest_fd, device_list);
		if (write(lguest_fd, args, sizeof(args)) < 0)
			err(1, "Resetting break");
	}
}

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

int main(int argc, char *argv[])
{
	unsigned long mem = 0, pgdir, start, page_offset, initrd_size = 0;
	int i, c, lguest_fd;
	struct device_list device_list;
	void *boot = (void *)0;
	const char *initrd_name = NULL;

	device_list.max_infd = -1;
	device_list.dev = NULL;
	device_list.lastdev = &device_list.dev;
	FD_ZERO(&device_list.infds);

	/* We need to know how much memory so we can allocate devices. */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			mem = top = atoi(argv[i]) * 1024 * 1024;
			device_list.descs = map_zeroed_pages(top, 1);
			top += getpagesize();
			break;
		}
	}
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
	if (optind + 2 > argc)
		usage();

	/* We need a console device */
	setup_console(&device_list);

	/* First we map /dev/zero over all of guest-physical memory. */
	map_zeroed_pages(0, mem / getpagesize());

	/* Now we load the kernel */
	start = load_kernel(open_or_die(argv[optind+1], O_RDONLY),
			    &page_offset);

	/* Map the initrd image if requested */
	if (initrd_name) {
		initrd_size = load_initrd(initrd_name, mem);
		*(unsigned long *)(boot+0x218) = mem - initrd_size;
		*(unsigned long *)(boot+0x21c) = initrd_size;
		*(unsigned char *)(boot+0x210) = 0xFF;
	}

	/* Set up the initial linar pagetables. */
	pgdir = setup_pagetables(mem, initrd_size, page_offset);

	/* E820 memory map: ours is a simple, single region. */
	*(char*)(boot+E820NR) = 1;
	*((struct e820entry *)(boot+E820MAP))
		= ((struct e820entry) { 0, mem, E820_RAM });
	/* Command line pointer and command line (at 4096) */
	*(void **)(boot + 0x228) = boot + 4096;
	concat(boot + 4096, argv+optind+2);
	/* Paravirt type: 1 == lguest */
	*(int *)(boot + 0x23c) = 1;

	lguest_fd = tell_kernel(pgdir, start, page_offset);
	waker_fd = setup_waker(lguest_fd, &device_list);

	run_guest(lguest_fd, &device_list);
}
