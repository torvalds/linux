/*
 *  linux/fs/9p/debug.h - V9FS Debug Definitions
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#define DEBUG_ERROR		(1<<0)
#define DEBUG_CURRENT		(1<<1)
#define DEBUG_9P	        (1<<2)
#define DEBUG_VFS	        (1<<3)
#define DEBUG_CONV		(1<<4)
#define DEBUG_MUX		(1<<5)
#define DEBUG_TRANS		(1<<6)
#define DEBUG_SLABS	      	(1<<7)

#define DEBUG_DUMP_PKT		0

extern int v9fs_debug_level;

#define dprintk(level, format, arg...) \
do {  \
	if((v9fs_debug_level & level)==level) \
		printk(KERN_NOTICE "-- %s (%d): " \
		format , __FUNCTION__, current->pid , ## arg); \
} while(0)

#define eprintk(level, format, arg...) \
do { \
	printk(level "v9fs: %s (%d): " \
		format , __FUNCTION__, current->pid , ## arg); \
} while(0)

#if DEBUG_DUMP_PKT
static inline void dump_data(const unsigned char *data, unsigned int datalen)
{
	int i, j;
	int len = datalen;

	printk(KERN_DEBUG "data ");
	for (i = 0; i < len; i += 4) {
		for (j = 0; (j < 4) && (i + j < len); j++)
			printk(KERN_DEBUG "%02x", data[i + j]);
		printk(KERN_DEBUG " ");
	}
	printk(KERN_DEBUG "\n");
}
#else				/* DEBUG_DUMP_PKT */
static inline void dump_data(const unsigned char *data, unsigned int datalen)
{

}
#endif				/* DEBUG_DUMP_PKT */
