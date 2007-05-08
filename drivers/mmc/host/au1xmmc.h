#ifndef _AU1XMMC_H_
#define _AU1XMMC_H_

/* Hardware definitions */

#define AU1XMMC_DESCRIPTOR_COUNT 1
#define AU1XMMC_DESCRIPTOR_SIZE  2048

#define AU1XMMC_OCR ( MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30  | \
		      MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33  | \
		      MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36)

/* Easy access macros */

#define HOST_STATUS(h)	((h)->iobase + SD_STATUS)
#define HOST_CONFIG(h)	((h)->iobase + SD_CONFIG)
#define HOST_ENABLE(h)	((h)->iobase + SD_ENABLE)
#define HOST_TXPORT(h)	((h)->iobase + SD_TXPORT)
#define HOST_RXPORT(h)	((h)->iobase + SD_RXPORT)
#define HOST_CMDARG(h)	((h)->iobase + SD_CMDARG)
#define HOST_BLKSIZE(h)	((h)->iobase + SD_BLKSIZE)
#define HOST_CMD(h)	((h)->iobase + SD_CMD)
#define HOST_CONFIG2(h)	((h)->iobase + SD_CONFIG2)
#define HOST_TIMEOUT(h)	((h)->iobase + SD_TIMEOUT)
#define HOST_DEBUG(h)	((h)->iobase + SD_DEBUG)

#define DMA_CHANNEL(h) \
	( ((h)->flags & HOST_F_XMIT) ? (h)->tx_chan : (h)->rx_chan)

/* This gives us a hard value for the stop command that we can write directly
 * to the command register
 */

#define STOP_CMD (SD_CMD_RT_1B|SD_CMD_CT_7|(0xC << SD_CMD_CI_SHIFT)|SD_CMD_GO)

/* This is the set of interrupts that we configure by default */

#if 0
#define AU1XMMC_INTERRUPTS (SD_CONFIG_SC | SD_CONFIG_DT | SD_CONFIG_DD | \
		SD_CONFIG_RAT | SD_CONFIG_CR | SD_CONFIG_I)
#endif

#define AU1XMMC_INTERRUPTS (SD_CONFIG_SC | SD_CONFIG_DT | \
		SD_CONFIG_RAT | SD_CONFIG_CR | SD_CONFIG_I)
/* The poll event (looking for insert/remove events runs twice a second */
#define AU1XMMC_DETECT_TIMEOUT (HZ/2)

struct au1xmmc_host {
  struct mmc_host *mmc;
  struct mmc_request *mrq;

  u32 id;

  u32 flags;
  u32 iobase;
  u32 clock;
  u32 bus_width;
  u32 power_mode;

  int status;

   struct {
	   int len;
	   int dir;
  } dma;

   struct {
	   int index;
	   int offset;
	   int len;
  } pio;

  u32 tx_chan;
  u32 rx_chan;

  struct timer_list timer;
  struct tasklet_struct finish_task;
  struct tasklet_struct data_task;

  spinlock_t lock;
};

/* Status flags used by the host structure */

#define HOST_F_XMIT   0x0001
#define HOST_F_RECV   0x0002
#define HOST_F_DMA    0x0010
#define HOST_F_ACTIVE 0x0100
#define HOST_F_STOP   0x1000

#define HOST_S_IDLE   0x0001
#define HOST_S_CMD    0x0002
#define HOST_S_DATA   0x0003
#define HOST_S_STOP   0x0004

#endif
