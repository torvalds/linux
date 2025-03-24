/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <type_support.h>

//CSI reveiver has 3 ports.
#define		N_CSI_PORTS (3)
//AM: Use previous define for this.

//MIPI allows up to 4 channels.
#define		N_CHANNELS  (4)
// 12KB = 256bit x 384 words
#define		IB_CAPACITY_IN_WORDS (384)

typedef enum {
	MIPI_0LANE_CFG = 0,
	MIPI_1LANE_CFG = 1,
	MIPI_2LANE_CFG = 2,
	MIPI_3LANE_CFG = 3,
	MIPI_4LANE_CFG = 4
} mipi_lane_cfg_t;

typedef enum {
	INPUT_SYSTEM_SOURCE_SENSOR = 0,
	INPUT_SYSTEM_SOURCE_FIFO,
	INPUT_SYSTEM_SOURCE_PRBS,
	INPUT_SYSTEM_SOURCE_MEMORY,
	N_INPUT_SYSTEM_SOURCE
} input_system_source_t;

/* internal routing configuration */
typedef enum {
	INPUT_SYSTEM_DISCARD_ALL = 0,
	INPUT_SYSTEM_CSI_BACKEND = 1,
	INPUT_SYSTEM_INPUT_BUFFER = 2,
	INPUT_SYSTEM_MULTICAST = 3,
	N_INPUT_SYSTEM_CONNECTION
} input_system_connection_t;

typedef enum {
	INPUT_SYSTEM_MIPI_PORT0,
	INPUT_SYSTEM_MIPI_PORT1,
	INPUT_SYSTEM_MIPI_PORT2,
	INPUT_SYSTEM_ACQUISITION_UNIT,
	N_INPUT_SYSTEM_MULTIPLEX
} input_system_multiplex_t;

typedef enum {
	INPUT_SYSTEM_SINK_MEMORY = 0,
	INPUT_SYSTEM_SINK_ISP,
	INPUT_SYSTEM_SINK_SP,
	N_INPUT_SYSTEM_SINK
} input_system_sink_t;

typedef enum {
	INPUT_SYSTEM_FIFO_CAPTURE = 0,
	INPUT_SYSTEM_FIFO_CAPTURE_WITH_COUNTING,
	INPUT_SYSTEM_SRAM_BUFFERING,
	INPUT_SYSTEM_XMEM_BUFFERING,
	INPUT_SYSTEM_XMEM_CAPTURE,
	INPUT_SYSTEM_XMEM_ACQUIRE,
	N_INPUT_SYSTEM_BUFFERING_MODE
} buffering_mode_t;

typedef struct isp2400_input_system_cfg_s	input_system_cfg_t;
typedef struct sync_generator_cfg_s	sync_generator_cfg_t;
typedef struct tpg_cfg_s			tpg_cfg_t;
typedef struct prbs_cfg_s			prbs_cfg_t;

/* MW: uint16_t should be sufficient */
struct isp2400_input_system_cfg_s {
	u32	no_side_band;
	u32	fmt_type;
	u32	ch_id;
	u32	input_mode;
};

struct sync_generator_cfg_s {
	u32	width;
	u32	height;
	u32	hblank_cycles;
	u32	vblank_cycles;
};

/* MW: tpg & prbs are exclusive */
struct tpg_cfg_s {
	u32	x_mask;
	u32	y_mask;
	u32	x_delta;
	u32	y_delta;
	u32	xy_mask;
	sync_generator_cfg_t sync_gen_cfg;
};

struct prbs_cfg_s {
	u32	seed;
	sync_generator_cfg_t sync_gen_cfg;
};

struct gpfifo_cfg_s {
// TBD.
	sync_generator_cfg_t sync_gen_cfg;
};

typedef struct gpfifo_cfg_s		gpfifo_cfg_t;

//ALX:Commented out to pass the compilation.
//typedef struct isp2400_input_system_cfg_s input_system_cfg_t;

struct ib_buffer_s {
	u32	mem_reg_size;
	u32	nof_mem_regs;
	u32	mem_reg_addr;
};

typedef struct ib_buffer_s	isp2400_ib_buffer_t;

struct csi_cfg_s {
	u32			csi_port;
	buffering_mode_t	buffering_mode;
	isp2400_ib_buffer_t	csi_buffer;
	isp2400_ib_buffer_t	acquisition_buffer;
	u32			nof_xmem_buffers;
};

typedef struct csi_cfg_s	 csi_cfg_t;

typedef enum {
	INPUT_SYSTEM_CFG_FLAG_RESET	= 0,
	INPUT_SYSTEM_CFG_FLAG_SET		= 1U << 0,
	INPUT_SYSTEM_CFG_FLAG_BLOCKED	= 1U << 1,
	INPUT_SYSTEM_CFG_FLAG_REQUIRED	= 1U << 2,
	INPUT_SYSTEM_CFG_FLAG_CONFLICT	= 1U << 3	// To mark a conflicting configuration.
} isp2400_input_system_cfg_flag_t;

typedef u32 input_system_config_flags_t;
