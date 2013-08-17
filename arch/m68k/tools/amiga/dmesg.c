/*
 *  linux/arch/m68k/tools/amiga/dmesg.c -- Retrieve the kernel messages stored
 *					   in Chip RAM with the kernel command
 *					   line option `debug=mem'.
 *
 *  © Copyright 1996 by Geert Uytterhoeven <geert@linux-m68k.org>
 *
 *
 *  Usage:
 *
 *	dmesg
 *	dmesg <CHIPMEM_END>
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define CHIPMEM_START	0x00000000
#define CHIPMEM_END	0x00200000	/* overridden by argv[1] */

#define SAVEKMSG_MAGIC1	0x53415645	/* 'SAVE' */
#define SAVEKMSG_MAGIC2	0x4B4D5347	/* 'KMSG' */

struct savekmsg {
    u_long magic1;	/* SAVEKMSG_MAGIC1 */
    u_long magic2;	/* SAVEKMSG_MAGIC2 */
    u_long magicptr;	/* address of magic1 */
    u_long size;
    char data[0];
};


int main(int argc, char *argv[])
{
    u_long start = CHIPMEM_START, end = CHIPMEM_END, p;
    int found = 0;
    struct savekmsg *m = NULL;

    if (argc >= 2)
	end = strtoul(argv[1], NULL, 0);
    printf("Searching for SAVEKMSG magic...\n");
    for (p = start; p <= end-sizeof(struct savekmsg); p += 4) {
	m = (struct savekmsg *)p;
	if ((m->magic1 == SAVEKMSG_MAGIC1) && (m->magic2 == SAVEKMSG_MAGIC2) &&
	    (m->magicptr == p)) {
	    found = 1;
	    break;
	}
    }
    if (!found)
	printf("Not found\n");
    else {
	printf("Found %ld bytes at 0x%08lx\n", m->size, (u_long)&m->data);
	puts(">>>>>>>>>>>>>>>>>>>>");
	fflush(stdout);
	write(1, &m->data, m->size);
	fflush(stdout);
	puts("<<<<<<<<<<<<<<<<<<<<");
    }
    return(0);
}
