/* linux/include/asm-arm/arch-msm/msm_smd.h
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_MSM_SMD_H
#define __ASM_ARCH_MSM_SMD_H

#include <linux/io.h>
#include <mach/msm_smsm.h>

typedef struct smd_channel smd_channel_t;

#define SMD_MAX_CH_NAME_LEN 20 /* includes null char at end */

#define SMD_EVENT_DATA 1
#define SMD_EVENT_OPEN 2
#define SMD_EVENT_CLOSE 3
#define SMD_EVENT_STATUS 4
#define SMD_EVENT_REOPEN_READY 5

/*
 * SMD Processor ID's.
 *
 * For all processors that have both SMSM and SMD clients,
 * the SMSM Processor ID and the SMD Processor ID will
 * be the same.  In cases where a processor only supports
 * SMD, the entry will only exist in this enum.
 */
enum {
	SMD_APPS = SMSM_APPS,
	SMD_MODEM = SMSM_MODEM,
	SMD_Q6 = SMSM_Q6,
	SMD_WCNSS = SMSM_WCNSS,
	SMD_DSPS = SMSM_DSPS,
	SMD_MODEM_Q6_FW,
	SMD_RPM,
	NUM_SMD_SUBSYSTEMS,
};

enum {
	SMD_APPS_MODEM = 0,
	SMD_APPS_QDSP,
	SMD_MODEM_QDSP,
	SMD_APPS_DSPS,
	SMD_MODEM_DSPS,
	SMD_QDSP_DSPS,
	SMD_APPS_WCNSS,
	SMD_MODEM_WCNSS,
	SMD_QDSP_WCNSS,
	SMD_DSPS_WCNSS,
	SMD_APPS_Q6FW,
	SMD_MODEM_Q6FW,
	SMD_QDSP_Q6FW,
	SMD_DSPS_Q6FW,
	SMD_WCNSS_Q6FW,
	SMD_APPS_RPM,
	SMD_MODEM_RPM,
	SMD_QDSP_RPM,
	SMD_WCNSS_RPM,
	SMD_NUM_TYPE,
	SMD_LOOPBACK_TYPE = 100,

};

/*
 * SMD IRQ Configuration
 *
 * Used to initialize IRQ configurations from platform data
 *
 * @irq_name: irq_name to query platform data
 * @irq_id: initialized to -1 in platform data, stores actual irq id on
 *		successful registration
 * @out_base: if not null then settings used for outgoing interrupt
 *		initialied from platform data
 */

struct smd_irq_config {
	/* incoming interrupt config */
	const char *irq_name;
	unsigned long flags;
	int irq_id;
	const char *device_name;
	const void *dev_id;

	/* outgoing interrupt config */
	uint32_t out_bit_pos;
	void __iomem *out_base;
	uint32_t out_offset;
};

/*
 * SMD subsystem configurations
 *
 * SMD subsystems configurations for platform data. This contains the
 * M2A and A2M interrupt configurations for both SMD and SMSM per
 * subsystem.
 *
 * @subsys_name: name of subsystem passed to PIL
 * @irq_config_id: unique id for each subsystem
 * @edge: maps to actual remote subsystem edge
 *
 */
struct smd_subsystem_config {
	unsigned irq_config_id;
	const char *subsys_name;
	int edge;

	struct smd_irq_config smd_int;
	struct smd_irq_config smsm_int;

};

/*
 * Subsystem Restart Configuration
 *
 * @disable_smsm_reset_handshake
 */
struct smd_subsystem_restart_config {
	int disable_smsm_reset_handshake;
};

struct smd_platform {
	uint32_t num_ss_configs;
	struct smd_subsystem_config *smd_ss_configs;
	struct smd_subsystem_restart_config *smd_ssr_config;
};

#ifdef CONFIG_MSM_SMD
/* warning: notify() may be called before open returns */
int smd_open(const char *name, smd_channel_t **ch, void *priv,
	     void (*notify)(void *priv, unsigned event));

int smd_close(smd_channel_t *ch);

/* passing a null pointer for data reads and discards */
int smd_read(smd_channel_t *ch, void *data, int len);
int smd_read_from_cb(smd_channel_t *ch, void *data, int len);
/* Same as smd_read() but takes a data buffer from userspace
 * The function might sleep.  Only safe to call from user context
 */
int smd_read_user_buffer(smd_channel_t *ch, void *data, int len);

/* Write to stream channels may do a partial write and return
** the length actually written.
** Write to packet channels will never do a partial write --
** it will return the requested length written or an error.
*/
int smd_write(smd_channel_t *ch, const void *data, int len);
/* Same as smd_write() but takes a data buffer from userspace
 * The function might sleep.  Only safe to call from user context
 */
int smd_write_user_buffer(smd_channel_t *ch, const void *data, int len);

int smd_write_avail(smd_channel_t *ch);
int smd_read_avail(smd_channel_t *ch);

/* Returns the total size of the current packet being read.
** Returns 0 if no packets available or a stream channel.
*/
int smd_cur_packet_size(smd_channel_t *ch);


#if 0
/* these are interruptable waits which will block you until the specified
** number of bytes are readable or writable.
*/
int smd_wait_until_readable(smd_channel_t *ch, int bytes);
int smd_wait_until_writable(smd_channel_t *ch, int bytes);
#endif

/* these are used to get and set the IF sigs of a channel.
 * DTR and RTS can be set; DSR, CTS, CD and RI can be read.
 */
int smd_tiocmget(smd_channel_t *ch);
int smd_tiocmset(smd_channel_t *ch, unsigned int set, unsigned int clear);
int
smd_tiocmset_from_cb(smd_channel_t *ch, unsigned int set, unsigned int clear);
int smd_named_open_on_edge(const char *name, uint32_t edge, smd_channel_t **_ch,
			   void *priv, void (*notify)(void *, unsigned));

/* Tells the other end of the smd channel that this end wants to recieve
 * interrupts when the written data is read.  Read interrupts should only
 * enabled when there is no space left in the buffer to write to, thus the
 * interrupt acts as notification that space may be avaliable.  If the
 * other side does not support enabling/disabling interrupts on demand,
 * then this function has no effect if called.
 */
void smd_enable_read_intr(smd_channel_t *ch);

/* Tells the other end of the smd channel that this end does not want
 * interrupts when written data is read.  The interrupts should be
 * disabled by default.  If the other side does not support enabling/
 * disabling interrupts on demand, then this function has no effect if
 * called.
 */
void smd_disable_read_intr(smd_channel_t *ch);

/* Starts a packet transaction.  The size of the packet may exceed the total
 * size of the smd ring buffer.
 *
 * @ch: channel to write the packet to
 * @len: total length of the packet
 *
 * Returns:
 *      0 - success
 *      -ENODEV - invalid smd channel
 *      -EACCES - non-packet channel specified
 *      -EINVAL - invalid length
 *      -EBUSY - transaction already in progress
 *      -EAGAIN - no enough memory in ring buffer to start transaction
 *      -EPERM - unable to sucessfully start transaction due to write error
 */
int smd_write_start(smd_channel_t *ch, int len);

/* Writes a segment of the packet for a packet transaction.
 *
 * @ch: channel to write packet to
 * @data: buffer of data to write
 * @len: length of data buffer
 * @user_buf: (0) - buffer from kernelspace    (1) - buffer from userspace
 *
 * Returns:
 *      number of bytes written
 *      -ENODEV - invalid smd channel
 *      -EINVAL - invalid length
 *      -ENOEXEC - transaction not started
 */
int smd_write_segment(smd_channel_t *ch, void *data, int len, int user_buf);

/* Completes a packet transaction.  Do not call from interrupt context.
 *
 * @ch: channel to complete transaction on
 *
 * Returns:
 *      0 - success
 *      -ENODEV - invalid smd channel
 *      -E2BIG - some ammount of packet is not yet written
 */
int smd_write_end(smd_channel_t *ch);

/*
 * Returns a pointer to the subsystem name or NULL if no
 * subsystem name is available.
 *
 * @type - Edge definition
 */
const char *smd_edge_to_subsystem(uint32_t type);

/*
 * Returns a pointer to the subsystem name given the
 * remote processor ID.
 *
 * @pid     Remote processor ID
 * @returns Pointer to subsystem name or NULL if not found
 */
const char *smd_pid_to_subsystem(uint32_t pid);
#else

static inline int smd_open(const char *name, smd_channel_t **ch, void *priv,
	     void (*notify)(void *priv, unsigned event))
{
	return -ENODEV;
}

static inline int smd_close(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline int smd_read(smd_channel_t *ch, void *data, int len)
{
	return -ENODEV;
}

static inline int smd_read_from_cb(smd_channel_t *ch, void *data, int len)
{
	return -ENODEV;
}

static inline int smd_read_user_buffer(smd_channel_t *ch, void *data, int len)
{
	return -ENODEV;
}

static inline int smd_write(smd_channel_t *ch, const void *data, int len)
{
	return -ENODEV;
}

static inline int
smd_write_user_buffer(smd_channel_t *ch, const void *data, int len)
{
	return -ENODEV;
}

static inline int smd_write_avail(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline int smd_read_avail(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline int smd_cur_packet_size(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline int smd_tiocmget(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline int
smd_tiocmset(smd_channel_t *ch, unsigned int set, unsigned int clear)
{
	return -ENODEV;
}

static inline int
smd_tiocmset_from_cb(smd_channel_t *ch, unsigned int set, unsigned int clear)
{
	return -ENODEV;
}

static inline int
smd_named_open_on_edge(const char *name, uint32_t edge, smd_channel_t **_ch,
			   void *priv, void (*notify)(void *, unsigned))
{
	return -ENODEV;
}

static inline void smd_enable_read_intr(smd_channel_t *ch)
{
}

static inline void smd_disable_read_intr(smd_channel_t *ch)
{
}

static inline int smd_write_start(smd_channel_t *ch, int len)
{
	return -ENODEV;
}

static inline int
smd_write_segment(smd_channel_t *ch, void *data, int len, int user_buf)
{
	return -ENODEV;
}

static inline int smd_write_end(smd_channel_t *ch)
{
	return -ENODEV;
}

static inline const char *smd_edge_to_subsystem(uint32_t type)
{
	return NULL;
}

static inline const char *smd_pid_to_subsystem(uint32_t pid)
{
	return NULL;
}
#endif

#endif
