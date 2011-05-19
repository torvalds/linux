#ifndef __LINUX_MFD_NVEC
#define __LINUX_MFD_NVEC

#include <linux/semaphore.h>

typedef enum {
	NVEC_2BYTES,
	NVEC_3BYTES,
	NVEC_VAR_SIZE
} nvec_size;

typedef enum {
	NOT_REALLY,
	YES,
	NOT_AT_ALL,
} how_care;

typedef enum {
	NVEC_SYS=1,
	NVEC_BAT,
	NVEC_KBD = 5,
	NVEC_PS2,
	NVEC_CNTL,
	NVEC_KB_EVT = 0x80,
	NVEC_PS2_EVT
} nvec_event;

typedef enum {
       NVEC_WAIT,
       NVEC_READ,
       NVEC_WRITE
} nvec_state;

struct nvec_msg {
	unsigned char *data;
	unsigned short size;
	unsigned short pos;
	struct list_head node;
};

struct nvec_subdev {
	const char *name;
	void *platform_data;
	int id;
};

struct nvec_platform_data {
	int num_subdevs;
	int i2c_addr;
	int gpio;
	int irq;
	int base;
	int size;
	char clock[16];
	struct nvec_subdev *subdevs;
};

struct nvec_chip {
	struct device *dev;
	int gpio;
	int irq;
	unsigned char *i2c_regs;
	nvec_state state;
	struct atomic_notifier_head notifier_list;
	struct list_head rx_data, tx_data;
	struct notifier_block nvec_status_notifier;
	struct work_struct rx_work, tx_work;
	struct nvec_msg *rx, *tx;

/* sync write stuff */
	struct semaphore sync_write_mutex;
	struct completion sync_write;
	u16 sync_write_pending;
	struct nvec_msg *last_sync_msg;
};

extern void nvec_write_async(struct nvec_chip *nvec, unsigned char *data, short size);

extern int nvec_register_notifier(struct nvec_chip *nvec,
		 struct notifier_block *nb, unsigned int events);

extern int nvec_unregister_notifier(struct device *dev,
		struct notifier_block *nb, unsigned int events);

const char *nvec_send_msg(unsigned char *src, unsigned char *dst_size, how_care care_resp, void (*rt_handler)(unsigned char *data));

extern int nvec_ps2(struct nvec_chip *nvec);
extern int nvec_kbd_init(struct nvec_chip *nvec);

#define I2C_CNFG			0x00
#define I2C_CNFG_PACKET_MODE_EN		(1<<10)
#define I2C_CNFG_NEW_MASTER_SFM		(1<<11)
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT	12

#define I2C_SL_CNFG		0x20
#define I2C_SL_NEWL		(1<<2)
#define I2C_SL_NACK		(1<<1)
#define I2C_SL_RESP		(1<<0)
#define I2C_SL_IRQ		(1<<3)
#define END_TRANS		(1<<4)
#define RCVD			(1<<2)
#define RNW			(1<<1)

#define I2C_SL_RCVD		0x24
#define I2C_SL_STATUS		0x28
#define I2C_SL_ADDR1		0x2c
#define I2C_SL_ADDR2		0x30
#define I2C_SL_DELAY_COUNT	0x3c

#endif
