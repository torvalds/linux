/*
 * arch/um/kernel/mem_user.c
 *
 * BRIEF MODULE DESCRIPTION
 * user side memory routines for supporting IO memory inside user mode linux
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon glonnon@ridgerun.com or info@ridgerun.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "kern_util.h"
#include "user.h"
#include "user_util.h"
#include "mem_user.h"
#include "init.h"
#include "os.h"
#include "tempfile.h"
#include "kern_constants.h"

#define TEMPNAME_TEMPLATE "vm_file-XXXXXX"

static int create_tmp_file(unsigned long len)
{
	int fd, err;
	char zero;

	fd = make_tempfile(TEMPNAME_TEMPLATE, NULL, 1);
	if(fd < 0) {
		os_print_error(fd, "make_tempfile");
		exit(1);
	}

	err = os_mode_fd(fd, 0777);
	if(err < 0){
		os_print_error(err, "os_mode_fd");
		exit(1);
	}
	err = os_seek_file(fd, len);
	if(err < 0){
		os_print_error(err, "os_seek_file");
		exit(1);
	}
	zero = 0;
	err = os_write_file(fd, &zero, 1);
	if(err != 1){
		os_print_error(err, "os_write_file");
		exit(1);
	}

	return(fd);
}

void check_tmpexec(void)
{
	void *addr;
	int err, fd = create_tmp_file(UM_KERN_PAGE_SIZE);

	addr = mmap(NULL, UM_KERN_PAGE_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
	printf("Checking PROT_EXEC mmap in /tmp...");
	fflush(stdout);
	if(addr == MAP_FAILED){
		err = errno;
		perror("failed");
		if(err == EPERM)
			printf("/tmp must be not mounted noexec\n");
		exit(1);
	}
	printf("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	os_close_file(fd);
}

static int have_devanon = 0;

void check_devanon(void)
{
	int fd;

	printk("Checking for /dev/anon on the host...");
	fd = open("/dev/anon", O_RDWR);
	if(fd < 0){
		printk("Not available (open failed with errno %d)\n", errno);
		return;
	}

	printk("OK\n");
	have_devanon = 1;
}

static int create_anon_file(unsigned long len)
{
	void *addr;
	int fd;

	fd = open("/dev/anon", O_RDWR);
	if(fd < 0) {
		os_print_error(fd, "opening /dev/anon");
		exit(1);
	}

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if(addr == MAP_FAILED){
		perror("mapping physmem file");
		exit(1);
	}
	munmap(addr, len);

	return(fd);
}

int create_mem_file(unsigned long len)
{
	int err, fd;

	if(have_devanon)
		fd = create_anon_file(len);
	else fd = create_tmp_file(len);

	err = os_set_exec_close(fd, 1);
	if(err < 0)
		os_print_error(err, "exec_close");
	return(fd);
}

struct iomem_region *iomem_regions = NULL;
int iomem_size = 0;

static int __init parse_iomem(char *str, int *add)
{
	struct iomem_region *new;
	struct uml_stat buf;
	char *file, *driver;
	int fd, err, size;

	driver = str;
	file = strchr(str,',');
	if(file == NULL){
		printf("parse_iomem : failed to parse iomem\n");
		goto out;
	}
	*file = '\0';
	file++;
	fd = os_open_file(file, of_rdwr(OPENFLAGS()), 0);
	if(fd < 0){
		os_print_error(fd, "parse_iomem - Couldn't open io file");
		goto out;
	}

	err = os_stat_fd(fd, &buf);
	if(err < 0){
		os_print_error(err, "parse_iomem - cannot stat_fd file");
		goto out_close;
	}

	new = malloc(sizeof(*new));
	if(new == NULL){
		perror("Couldn't allocate iomem_region struct");
		goto out_close;
	}

	size = (buf.ust_size + UM_KERN_PAGE_SIZE) & ~(UM_KERN_PAGE_SIZE - 1);

	*new = ((struct iomem_region) { .next		= iomem_regions,
					.driver		= driver,
					.fd		= fd,
					.size		= size,
					.phys		= 0,
					.virt		= 0 });
	iomem_regions = new;
	iomem_size += new->size + UM_KERN_PAGE_SIZE;

	return(0);
 out_close:
	os_close_file(fd);
 out:
	return(1);
}

__uml_setup("iomem=", parse_iomem,
"iomem=<name>,<file>\n"
"    Configure <file> as an IO memory region named <name>.\n\n"
);

int protect_memory(unsigned long addr, unsigned long len, int r, int w, int x,
		   int must_succeed)
{
	int err;

	err = os_protect_memory((void *) addr, len, r, w, x);
	if(err < 0){
                if(must_succeed)
			panic("protect failed, err = %d", -err);
		else return(err);
	}
	return(0);
}

#if 0
/* Debugging facility for dumping stuff out to the host, avoiding the timing
 * problems that come with printf and breakpoints.
 * Enable in case of emergency.
 */

int logging = 1;
int logging_fd = -1;

int logging_line = 0;
char logging_buf[512];

void log(char *fmt, ...)
{
        va_list ap;
        struct timeval tv;
        struct openflags flags;

        if(logging == 0) return;
        if(logging_fd < 0){
                flags = of_create(of_trunc(of_rdwr(OPENFLAGS())));
                logging_fd = os_open_file("log", flags, 0644);
        }
        gettimeofday(&tv, NULL);
        sprintf(logging_buf, "%d\t %u.%u  ", logging_line++, tv.tv_sec,
                tv.tv_usec);
        va_start(ap, fmt);
        vsprintf(&logging_buf[strlen(logging_buf)], fmt, ap);
        va_end(ap);
        write(logging_fd, logging_buf, strlen(logging_buf));
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
