/*
 * arch/um/include/mem_user.h
 *
 * BRIEF MODULE DESCRIPTION
 * user side memory interface for support IO memory inside user mode linux
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

#ifndef _MEM_USER_H
#define _MEM_USER_H

struct iomem_region {
	struct iomem_region *next;
	char *driver;
	int fd;
	int size;
	unsigned long phys;
	unsigned long virt;
};

extern struct iomem_region *iomem_regions;
extern int iomem_size;

#define ROUND_4M(n) ((((unsigned long) (n)) + (1 << 22)) & ~((1 << 22) - 1))

extern unsigned long host_task_size;
extern unsigned long task_size;

extern void check_devanon(void);
extern int init_mem_user(void);
extern void setup_memory(void *entry);
extern unsigned long find_iomem(char *driver, unsigned long *len_out);
extern int init_maps(unsigned long physmem, unsigned long iomem,
		     unsigned long highmem);
extern unsigned long get_vm(unsigned long len);
extern void setup_physmem(unsigned long start, unsigned long usable,
			  unsigned long len, unsigned long highmem);
extern void add_iomem(char *name, int fd, unsigned long size);
extern unsigned long phys_offset(unsigned long phys);
extern void unmap_physmem(void);
extern void map_memory(unsigned long virt, unsigned long phys,
		       unsigned long len, int r, int w, int x);
extern unsigned long get_kmem_end(void);

#endif
