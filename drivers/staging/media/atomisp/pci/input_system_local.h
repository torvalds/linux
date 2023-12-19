/* SPDX-License-Identifier: GPL-2.0 */
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *    (c) 2020 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
 */

#include "type_support.h"
#include "input_system_global.h"

typedef enum {
	INPUT_SYSTEM_PORT_A = 0,
	INPUT_SYSTEM_PORT_B,
	INPUT_SYSTEM_PORT_C,
	N_INPUT_SYSTEM_PORTS
} input_system_csi_port_t;

typedef struct ctrl_unit_cfg_s			ctrl_unit_cfg_t;
typedef struct input_system_network_cfg_s	input_system_network_cfg_t;
typedef struct target_cfg2400_s		target_cfg2400_t;
typedef struct channel_cfg_s			channel_cfg_t;
typedef struct backend_channel_cfg_s		backend_channel_cfg_t;
typedef struct input_system_cfg2400_s		input_system_cfg2400_t;
typedef struct mipi_port_state_s		mipi_port_state_t;
typedef struct rx_channel_state_s		rx_channel_state_t;
typedef struct input_switch_cfg_channel_s	input_switch_cfg_channel_t;
typedef struct input_switch_cfg_s		input_switch_cfg_t;

struct ctrl_unit_cfg_s {
	isp2400_ib_buffer_t		buffer_mipi[N_CAPTURE_UNIT_ID];
	isp2400_ib_buffer_t		buffer_acquire[N_ACQUISITION_UNIT_ID];
};

struct input_system_network_cfg_s {
	input_system_connection_t	multicast_cfg[N_CAPTURE_UNIT_ID];
	input_system_multiplex_t	mux_cfg;
	ctrl_unit_cfg_t				ctrl_unit_cfg[N_CTRL_UNIT_ID];
};

typedef struct {
// TBD.
	u32	dummy_parameter;
} target_isp_cfg_t;

typedef struct {
// TBD.
	u32	dummy_parameter;
} target_sp_cfg_t;

typedef struct {
// TBD.
	u32	dummy_parameter;
} target_strm2mem_cfg_t;

struct input_switch_cfg_channel_s {
	u32 hsync_data_reg[2];
	u32 vsync_data_reg;
};

struct backend_channel_cfg_s {
	u32	fmt_control_word_1; // Format config.
	u32	fmt_control_word_2;
	u32	no_side_band;
};

typedef union  {
	csi_cfg_t	csi_cfg;
	tpg_cfg_t	tpg_cfg;
	prbs_cfg_t	prbs_cfg;
	gpfifo_cfg_t	gpfifo_cfg;
} source_cfg_t;

struct input_switch_cfg_s {
	u32 hsync_data_reg[N_RX_CHANNEL_ID * 2];
	u32 vsync_data_reg;
};

/*
 * In 2300 ports can be configured independently and stream
 * formats need to be specified. In 2400, there are only 8
 * supported configurations but the HW is fused to support
 * only a single one.
 *
 * In 2300 the compressed format types are programmed by the
 * user. In 2400 all stream formats are encoded on the stream.
 *
 * Use the enum to check validity of a user configuration
 */
typedef enum {
	MONO_4L_1L_0L = 0,
	MONO_3L_1L_0L,
	MONO_2L_1L_0L,
	MONO_1L_1L_0L,
	STEREO_2L_1L_2L,
	STEREO_3L_1L_1L,
	STEREO_2L_1L_1L,
	STEREO_1L_1L_1L,
	N_RX_MODE
} rx_mode_t;

#define UNCOMPRESSED_BITS_PER_PIXEL_10	10
#define UNCOMPRESSED_BITS_PER_PIXEL_12	12
#define COMPRESSED_BITS_PER_PIXEL_6	6
#define COMPRESSED_BITS_PER_PIXEL_7	7
#define COMPRESSED_BITS_PER_PIXEL_8	8
enum mipi_compressor {
	MIPI_COMPRESSOR_NONE = 0,
	MIPI_COMPRESSOR_10_6_10,
	MIPI_COMPRESSOR_10_7_10,
	MIPI_COMPRESSOR_10_8_10,
	MIPI_COMPRESSOR_12_6_12,
	MIPI_COMPRESSOR_12_7_12,
	MIPI_COMPRESSOR_12_8_12,
	N_MIPI_COMPRESSOR_METHODS
};

typedef enum mipi_compressor mipi_compressor_t;

typedef enum {
	MIPI_PREDICTOR_NONE = 0,
	MIPI_PREDICTOR_TYPE1,
	MIPI_PREDICTOR_TYPE2,
	N_MIPI_PREDICTOR_TYPES
} mipi_predictor_t;

typedef struct rx_cfg_s		rx_cfg_t;

/*
 * Applied per port
 */
struct rx_cfg_s {
	rx_mode_t			mode;	/* The HW config */
	enum mipi_port_id		port;	/* The port ID to apply the control on */
	unsigned int		timeout;
	unsigned int		initcount;
	unsigned int		synccount;
	unsigned int		rxcount;
	mipi_predictor_t	comp;	/* Just for backward compatibility */
	bool                is_two_ppc;
};

#include "isp2401_input_system_local.h"
#include "isp2400_input_system_local.h"
