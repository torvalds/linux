
#ifndef _IEEE1394_CSR_H
#define _IEEE1394_CSR_H

#ifdef CONFIG_PREEMPT
#include <linux/sched.h>
#endif

#include "csr1212.h"

#define CSR_REGISTER_BASE  0xfffff0000000ULL

/* register offsets relative to CSR_REGISTER_BASE */
#define CSR_STATE_CLEAR           0x0
#define CSR_STATE_SET             0x4
#define CSR_NODE_IDS              0x8
#define CSR_RESET_START           0xc
#define CSR_SPLIT_TIMEOUT_HI      0x18
#define CSR_SPLIT_TIMEOUT_LO      0x1c
#define CSR_CYCLE_TIME            0x200
#define CSR_BUS_TIME              0x204
#define CSR_BUSY_TIMEOUT          0x210
#define CSR_BUS_MANAGER_ID        0x21c
#define CSR_BANDWIDTH_AVAILABLE   0x220
#define CSR_CHANNELS_AVAILABLE    0x224
#define CSR_CHANNELS_AVAILABLE_HI 0x224
#define CSR_CHANNELS_AVAILABLE_LO 0x228
#define CSR_BROADCAST_CHANNEL     0x234
#define CSR_CONFIG_ROM            0x400
#define CSR_CONFIG_ROM_END        0x800
#define CSR_FCP_COMMAND           0xB00
#define CSR_FCP_RESPONSE          0xD00
#define CSR_FCP_END               0xF00
#define CSR_TOPOLOGY_MAP          0x1000
#define CSR_TOPOLOGY_MAP_END      0x1400
#define CSR_SPEED_MAP             0x2000
#define CSR_SPEED_MAP_END         0x3000

/* IEEE 1394 bus specific Configuration ROM Key IDs */
#define IEEE1394_KV_ID_POWER_REQUIREMENTS (0x30)

/* IEEE 1394 Bus Inforamation Block specifics */
#define CSR_BUS_INFO_SIZE (5 * sizeof(quadlet_t))

#define CSR_IRMC_SHIFT 31
#define CSR_CMC_SHIFT  30
#define CSR_ISC_SHIFT  29
#define CSR_BMC_SHIFT  28
#define CSR_PMC_SHIFT  27
#define CSR_CYC_CLK_ACC_SHIFT 16
#define CSR_MAX_REC_SHIFT 12
#define CSR_MAX_ROM_SHIFT 8
#define CSR_GENERATION_SHIFT 4

#define CSR_SET_BUS_INFO_GENERATION(csr, gen)				\
	((csr)->bus_info_data[2] =					\
		cpu_to_be32((be32_to_cpu((csr)->bus_info_data[2]) &	\
			     ~(0xf << CSR_GENERATION_SHIFT)) |          \
			    (gen) << CSR_GENERATION_SHIFT))

struct csr_control {
        spinlock_t lock;

        quadlet_t state;
        quadlet_t node_ids;
        quadlet_t split_timeout_hi, split_timeout_lo;
	unsigned long expire;	// Calculated from split_timeout
        quadlet_t cycle_time;
        quadlet_t bus_time;
        quadlet_t bus_manager_id;
        quadlet_t bandwidth_available;
        quadlet_t channels_available_hi, channels_available_lo;
	quadlet_t broadcast_channel;

	/* Bus Info */
	quadlet_t guid_hi, guid_lo;
	u8 cyc_clk_acc;
	u8 max_rec;
	u8 max_rom;
	u8 generation;	/* Only use values between 0x2 and 0xf */
	u8 lnk_spd;

	unsigned long gen_timestamp[16];

	struct csr1212_csr *rom;

        quadlet_t topology_map[256];
        quadlet_t speed_map[1024];
};

extern struct csr1212_bus_ops csr_bus_ops;

int init_csr(void);
void cleanup_csr(void);

#endif /* _IEEE1394_CSR_H */
