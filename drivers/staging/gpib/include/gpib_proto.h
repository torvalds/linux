/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIB_PROTO_INCLUDED
#define GPIB_PROTO_INCLUDED

#include <linux/fs.h>

int ibopen(struct inode *inode, struct file *filep);
int ibclose(struct inode *inode, struct file *file);
long ibioctl(struct file *filep, unsigned int cmd, unsigned long arg);
int osInit(void);
void osReset(void);
void os_start_timer(struct gpib_board *board, unsigned int usec_timeout);
void os_remove_timer(struct gpib_board *board);
void osSendEOI(void);
void osSendEOI(void);
void init_gpib_board(struct gpib_board *board);
static inline unsigned long usec_to_jiffies(unsigned int usec)
{
	unsigned long usec_per_jiffy = 1000000 / HZ;

	return 1 + (usec + usec_per_jiffy - 1) / usec_per_jiffy;
};

int serial_poll_all(struct gpib_board *board, unsigned int usec_timeout);
void init_gpib_descriptor(gpib_descriptor_t *desc);
int dvrsp(struct gpib_board *board, unsigned int pad, int sad,
	  unsigned int usec_timeout, uint8_t *result);
int ibAPWait(struct gpib_board *board, int pad);
int ibAPrsp(struct gpib_board *board, int padsad, char *spb);
void ibAPE(struct gpib_board *board, int pad, int v);
int ibcac(struct gpib_board *board, int sync, int fallback_to_async);
int ibcmd(struct gpib_board *board, uint8_t *buf, size_t length, size_t *bytes_written);
int ibgts(struct gpib_board *board);
int ibonline(struct gpib_board *board);
int iboffline(struct gpib_board *board);
int iblines(const struct gpib_board *board, short *lines);
int ibrd(struct gpib_board *board, uint8_t *buf, size_t length, int *end_flag, size_t *bytes_read);
int ibrpp(struct gpib_board *board, uint8_t *buf);
int ibrsv2(struct gpib_board *board, uint8_t status_byte, int new_reason_for_service);
void ibrsc(struct gpib_board *board, int request_control);
int ibsic(struct gpib_board *board, unsigned int usec_duration);
int ibsre(struct gpib_board *board, int enable);
int ibpad(struct gpib_board *board, unsigned int addr);
int ibsad(struct gpib_board *board, int addr);
int ibeos(struct gpib_board *board, int eos, int eosflags);
int ibwait(struct gpib_board *board, int wait_mask, int clear_mask, int set_mask,
	   int *status, unsigned long usec_timeout, gpib_descriptor_t *desc);
int ibwrt(struct gpib_board *board, uint8_t *buf, size_t cnt, int send_eoi, size_t *bytes_written);
int ibstatus(struct gpib_board *board);
int general_ibstatus(struct gpib_board *board, const gpib_status_queue_t *device,
		     int clear_mask, int set_mask, gpib_descriptor_t *desc);
int io_timed_out(struct gpib_board *board);
int ibppc(struct gpib_board *board, uint8_t configuration);

#endif /* GPIB_PROTO_INCLUDED */
