/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIB_PROTO_INCLUDED
#define GPIB_PROTO_INCLUDED

#include <linux/fs.h>

int ibopen(struct inode *inode, struct file *filep);
int ibclose(struct inode *inode, struct file *file);
long ibioctl(struct file *filep, unsigned int cmd, unsigned long arg);
int osInit(void);
void osReset(void);
void os_start_timer(gpib_board_t *board, unsigned int usec_timeout);
void os_remove_timer(gpib_board_t *board);
void osSendEOI(void);
void osSendEOI(void);
void init_gpib_board(gpib_board_t *board);
static inline unsigned long usec_to_jiffies(unsigned int usec)
{
	unsigned long usec_per_jiffy = 1000000 / HZ;

	return 1 + (usec + usec_per_jiffy - 1) / usec_per_jiffy;
};

int serial_poll_all(gpib_board_t *board, unsigned int usec_timeout);
void init_gpib_descriptor(gpib_descriptor_t *desc);
int dvrsp(gpib_board_t *board, unsigned int pad, int sad,
	  unsigned int usec_timeout, uint8_t *result);
int ibAPWait(gpib_board_t *board, int pad);
int ibAPrsp(gpib_board_t *board, int padsad, char *spb);
void ibAPE(gpib_board_t *board, int pad, int v);
int ibcac(gpib_board_t *board, int sync, int fallback_to_async);
int ibcmd(gpib_board_t *board, uint8_t *buf, size_t length, size_t *bytes_written);
int ibgts(gpib_board_t *board);
int ibonline(gpib_board_t *board);
int iboffline(gpib_board_t *board);
int iblines(const gpib_board_t *board, short *lines);
int ibrd(gpib_board_t *board, uint8_t *buf, size_t length, int *end_flag, size_t *bytes_read);
int ibrpp(gpib_board_t *board, uint8_t *buf);
int ibrsv2(gpib_board_t *board, uint8_t status_byte, int new_reason_for_service);
void ibrsc(gpib_board_t *board, int request_control);
int ibsic(gpib_board_t *board, unsigned int usec_duration);
int ibsre(gpib_board_t *board, int enable);
int ibpad(gpib_board_t *board, unsigned int addr);
int ibsad(gpib_board_t *board, int addr);
int ibeos(gpib_board_t *board, int eos, int eosflags);
int ibwait(gpib_board_t *board, int wait_mask, int clear_mask, int set_mask,
	   int *status, unsigned long usec_timeout, gpib_descriptor_t *desc);
int ibwrt(gpib_board_t *board, uint8_t *buf, size_t cnt, int send_eoi, size_t *bytes_written);
int ibstatus(gpib_board_t *board);
int general_ibstatus(gpib_board_t *board, const gpib_status_queue_t *device,
		     int clear_mask, int set_mask, gpib_descriptor_t *desc);
int io_timed_out(gpib_board_t *board);
int ibppc(gpib_board_t *board, uint8_t configuration);

#endif /* GPIB_PROTO_INCLUDED */
