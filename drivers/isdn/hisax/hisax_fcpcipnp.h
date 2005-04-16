#include "hisax_if.h"
#include "hisax_isac.h"
#include <linux/pci.h>

#define HSCX_BUFMAX	4096

enum {
	AVM_FRITZ_PCI,
	AVM_FRITZ_PNP,
	AVM_FRITZ_PCIV2,
};

struct hdlc_stat_reg {
#ifdef __BIG_ENDIAN
	u_char fill __attribute__((packed));
	u_char mode __attribute__((packed));
	u_char xml  __attribute__((packed));
	u_char cmd  __attribute__((packed));
#else
	u_char cmd  __attribute__((packed));
	u_char xml  __attribute__((packed));
	u_char mode __attribute__((packed));
	u_char fill __attribute__((packed));
#endif
};

struct fritz_bcs {
	struct hisax_b_if b_if;
	struct fritz_adapter *adapter;
	int mode;
	int channel;

	union {
		u_int ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u_int stat;
	int rcvidx;
	int fifo_size;
	u_char rcvbuf[HSCX_BUFMAX]; /* B-Channel receive Buffer */
	
	int tx_cnt;		    /* B-Channel transmit counter */
	struct sk_buff *tx_skb;     /* B-Channel transmit Buffer */
};

struct fritz_adapter {
	int type;
	spinlock_t hw_lock;
	unsigned int io;
	unsigned int irq;
	struct isac isac;

	struct fritz_bcs bcs[2];

	u32  (*read_hdlc_status) (struct fritz_adapter *adapter, int nr);
	void (*write_ctrl) (struct fritz_bcs *bcs, int which);
};

