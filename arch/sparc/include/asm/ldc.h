#ifndef _SPARC64_LDC_H
#define _SPARC64_LDC_H

#include <asm/hypervisor.h>

extern int ldom_domaining_enabled;
void ldom_set_var(const char *var, const char *value);
void ldom_reboot(const char *boot_command);
void ldom_power_off(void);

/* The event handler will be evoked when link state changes
 * or data becomes available on the receive side.
 *
 * For non-RAW links, if the LDC_EVENT_RESET event arrives the
 * driver should reset all of it's internal state and reinvoke
 * ldc_connect() to try and bring the link up again.
 *
 * For RAW links, ldc_connect() is not used.  Instead the driver
 * just waits for the LDC_EVENT_UP event.
 */
struct ldc_channel_config {
	void (*event)(void *arg, int event);

	u32			mtu;
	unsigned int		rx_irq;
	unsigned int		tx_irq;
	u8			mode;
#define LDC_MODE_RAW		0x00
#define LDC_MODE_UNRELIABLE	0x01
#define LDC_MODE_RESERVED	0x02
#define LDC_MODE_STREAM		0x03

	u8			debug;
#define LDC_DEBUG_HS		0x01
#define LDC_DEBUG_STATE		0x02
#define LDC_DEBUG_RX		0x04
#define LDC_DEBUG_TX		0x08
#define LDC_DEBUG_DATA		0x10
};

#define LDC_EVENT_RESET		0x01
#define LDC_EVENT_UP		0x02
#define LDC_EVENT_DATA_READY	0x04

#define LDC_STATE_INVALID	0x00
#define LDC_STATE_INIT		0x01
#define LDC_STATE_BOUND		0x02
#define LDC_STATE_READY		0x03
#define LDC_STATE_CONNECTED	0x04

struct ldc_channel;

/* Allocate state for a channel.  */
struct ldc_channel *ldc_alloc(unsigned long id,
			      const struct ldc_channel_config *cfgp,
			      void *event_arg);

/* Shut down and free state for a channel.  */
void ldc_free(struct ldc_channel *lp);

/* Register TX and RX queues of the link with the hypervisor.  */
int ldc_bind(struct ldc_channel *lp, const char *name);

/* For non-RAW protocols we need to complete a handshake before
 * communication can proceed.  ldc_connect() does that, if the
 * handshake completes successfully, an LDC_EVENT_UP event will
 * be sent up to the driver.
 */
int ldc_connect(struct ldc_channel *lp);
int ldc_disconnect(struct ldc_channel *lp);

int ldc_state(struct ldc_channel *lp);

/* Read and write operations.  Only valid when the link is up.  */
int ldc_write(struct ldc_channel *lp, const void *buf,
	      unsigned int size);
int ldc_read(struct ldc_channel *lp, void *buf, unsigned int size);

#define LDC_MAP_SHADOW	0x01
#define LDC_MAP_DIRECT	0x02
#define LDC_MAP_IO	0x04
#define LDC_MAP_R	0x08
#define LDC_MAP_W	0x10
#define LDC_MAP_X	0x20
#define LDC_MAP_RW	(LDC_MAP_R | LDC_MAP_W)
#define LDC_MAP_RWX	(LDC_MAP_R | LDC_MAP_W | LDC_MAP_X)
#define LDC_MAP_ALL	0x03f

struct ldc_trans_cookie {
	u64			cookie_addr;
	u64			cookie_size;
};

struct scatterlist;
int ldc_map_sg(struct ldc_channel *lp,
	       struct scatterlist *sg, int num_sg,
	       struct ldc_trans_cookie *cookies, int ncookies,
	       unsigned int map_perm);

int ldc_map_single(struct ldc_channel *lp,
		   void *buf, unsigned int len,
		   struct ldc_trans_cookie *cookies, int ncookies,
		   unsigned int map_perm);

void ldc_unmap(struct ldc_channel *lp, struct ldc_trans_cookie *cookies,
	       int ncookies);

int ldc_copy(struct ldc_channel *lp, int copy_dir,
	     void *buf, unsigned int len, unsigned long offset,
	     struct ldc_trans_cookie *cookies, int ncookies);

static inline int ldc_get_dring_entry(struct ldc_channel *lp,
				      void *buf, unsigned int len,
				      unsigned long offset,
				      struct ldc_trans_cookie *cookies,
				      int ncookies)
{
	return ldc_copy(lp, LDC_COPY_IN, buf, len, offset, cookies, ncookies);
}

static inline int ldc_put_dring_entry(struct ldc_channel *lp,
				      void *buf, unsigned int len,
				      unsigned long offset,
				      struct ldc_trans_cookie *cookies,
				      int ncookies)
{
	return ldc_copy(lp, LDC_COPY_OUT, buf, len, offset, cookies, ncookies);
}

void *ldc_alloc_exp_dring(struct ldc_channel *lp, unsigned int len,
			  struct ldc_trans_cookie *cookies,
			  int *ncookies, unsigned int map_perm);

void ldc_free_exp_dring(struct ldc_channel *lp, void *buf,
		        unsigned int len,
		        struct ldc_trans_cookie *cookies, int ncookies);

#endif /* _SPARC64_LDC_H */
