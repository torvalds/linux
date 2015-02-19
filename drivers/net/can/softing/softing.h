/*
 * softing common interfaces
 *
 * by Kurt Van Dijck, 2008-2010
 */

#include <linux/atomic.h>
#include <linux/netdevice.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/can.h>
#include <linux/can/dev.h>

#include "softing_platform.h"

struct softing;

struct softing_priv {
	struct can_priv can; /* must be the first member! */
	struct net_device *netdev;
	struct softing *card;
	struct {
		int pending;
		/* variables which hold the circular buffer */
		int echo_put;
		int echo_get;
	} tx;
	struct can_bittiming_const btr_const;
	int index;
	uint8_t output;
	uint16_t chip;
};
#define netdev2softing(netdev)	((struct softing_priv *)netdev_priv(netdev))

struct softing {
	const struct softing_platform_data *pdat;
	struct platform_device *pdev;
	struct net_device *net[2];
	spinlock_t spin; /* protect this structure & DPRAM access */
	ktime_t ts_ref;
	ktime_t ts_overflow; /* timestamp overflow value, in ktime */

	struct {
		/* indication of firmware status */
		int up;
		/* protection of the 'up' variable */
		struct mutex lock;
	} fw;
	struct {
		int nr;
		int requested;
		int svc_count;
		unsigned int dpram_position;
	} irq;
	struct {
		int pending;
		int last_bus;
		/*
		 * keep the bus that last tx'd a message,
		 * in order to let every netdev queue resume
		 */
	} tx;
	__iomem uint8_t *dpram;
	unsigned long dpram_phys;
	unsigned long dpram_size;
	struct {
		uint16_t fw_version, hw_version, license, serial;
		uint16_t chip[2];
		unsigned int freq; /* remote cpu's operating frequency */
	} id;
};

int softing_default_output(struct net_device *netdev);

ktime_t softing_raw2ktime(struct softing *card, u32 raw);

int softing_chip_poweron(struct softing *card);

int softing_bootloader_command(struct softing *card, int16_t cmd,
			       const char *msg);

/* Load firmware after reset */
int softing_load_fw(const char *file, struct softing *card,
		    __iomem uint8_t *virt, unsigned int size, int offset);

/* Load final application firmware after bootloader */
int softing_load_app_fw(const char *file, struct softing *card);

/*
 * enable or disable irq
 * only called with fw.lock locked
 */
int softing_enable_irq(struct softing *card, int enable);

/* start/stop 1 bus on card */
int softing_startstop(struct net_device *netdev, int up);

/* netif_rx() */
int softing_netdev_rx(struct net_device *netdev, const struct can_frame *msg,
		      ktime_t ktime);

/* SOFTING DPRAM mappings */
#define DPRAM_RX		0x0000
	#define DPRAM_RX_SIZE	32
	#define DPRAM_RX_CNT	16
#define DPRAM_RX_RD		0x0201	/* uint8_t */
#define DPRAM_RX_WR		0x0205	/* uint8_t */
#define DPRAM_RX_LOST		0x0207	/* uint8_t */

#define DPRAM_FCT_PARAM		0x0300	/* int16_t [20] */
#define DPRAM_FCT_RESULT	0x0328	/* int16_t */
#define DPRAM_FCT_HOST		0x032b	/* uint16_t */

#define DPRAM_INFO_BUSSTATE	0x0331	/* uint16_t */
#define DPRAM_INFO_BUSSTATE2	0x0335	/* uint16_t */
#define DPRAM_INFO_ERRSTATE	0x0339	/* uint16_t */
#define DPRAM_INFO_ERRSTATE2	0x033d	/* uint16_t */
#define DPRAM_RESET		0x0341	/* uint16_t */
#define DPRAM_CLR_RECV_FIFO	0x0345	/* uint16_t */
#define DPRAM_RESET_TIME	0x034d	/* uint16_t */
#define DPRAM_TIME		0x0350	/* uint64_t */
#define DPRAM_WR_START		0x0358	/* uint8_t */
#define DPRAM_WR_END		0x0359	/* uint8_t */
#define DPRAM_RESET_RX_FIFO	0x0361	/* uint16_t */
#define DPRAM_RESET_TX_FIFO	0x0364	/* uint8_t */
#define DPRAM_READ_FIFO_LEVEL	0x0365	/* uint8_t */
#define DPRAM_RX_FIFO_LEVEL	0x0366	/* uint16_t */
#define DPRAM_TX_FIFO_LEVEL	0x0366	/* uint16_t */

#define DPRAM_TX		0x0400	/* uint16_t */
	#define DPRAM_TX_SIZE	16
	#define DPRAM_TX_CNT	32
#define DPRAM_TX_RD		0x0601	/* uint8_t */
#define DPRAM_TX_WR		0x0605	/* uint8_t */

#define DPRAM_COMMAND		0x07e0	/* uint16_t */
#define DPRAM_RECEIPT		0x07f0	/* uint16_t */
#define DPRAM_IRQ_TOHOST	0x07fe	/* uint8_t */
#define DPRAM_IRQ_TOCARD	0x07ff	/* uint8_t */

#define DPRAM_V2_RESET		0x0e00	/* uint8_t */
#define DPRAM_V2_IRQ_TOHOST	0x0e02	/* uint8_t */

#define TXMAX	(DPRAM_TX_CNT - 1)

/* DPRAM return codes */
#define RES_NONE	0
#define RES_OK		1
#define RES_NOK		2
#define RES_UNKNOWN	3
/* DPRAM flags */
#define CMD_TX		0x01
#define CMD_ACK		0x02
#define CMD_XTD		0x04
#define CMD_RTR		0x08
#define CMD_ERR		0x10
#define CMD_BUS2	0x80

/* returned fifo entry bus state masks */
#define SF_MASK_BUSOFF		0x80
#define SF_MASK_EPASSIVE	0x60

/* bus states */
#define STATE_BUSOFF	2
#define STATE_EPASSIVE	1
#define STATE_EACTIVE	0
