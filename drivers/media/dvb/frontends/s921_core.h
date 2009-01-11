#ifndef _S921_CORE_H
#define _S921_CORE_H
//#define u8 unsigned int
//#define u32 unsigned int



//#define EINVAL -1
#define E_OK 0

struct s921_isdb_t {
	void *priv_dev;
	int (*i2c_write)(void *dev, u8 reg, u8 val);
	int (*i2c_read)(void *dev, u8 reg);
};

#define ISDB_T_CMD_INIT       0
#define ISDB_T_CMD_SET_PARAM  1
#define ISDB_T_CMD_TUNE       2
#define ISDB_T_CMD_GET_STATUS 3

struct s921_isdb_t_tune_params {
	u32 frequency;
};

struct s921_isdb_t_status {
};

struct s921_isdb_t_transmission_mode_params {
	u8 mode;
	u8 layer_a_mode;
#define ISDB_T_LA_MODE_1 0
#define ISDB_T_LA_MODE_2 1
#define ISDB_T_LA_MODE_3 2
	u8 layer_a_carrier_modulation;
#define ISDB_T_LA_CM_DQPSK 0
#define ISDB_T_LA_CM_QPSK  1
#define ISDB_T_LA_CM_16QAM 2
#define ISDB_T_LA_CM_64QAM 3
#define ISDB_T_LA_CM_NOLAYER 4
	u8 layer_a_code_rate;
#define ISDB_T_LA_CR_1_2   0
#define ISDB_T_LA_CR_2_3   1
#define ISDB_T_LA_CR_3_4   2
#define ISDB_T_LA_CR_5_6   4
#define ISDB_T_LA_CR_7_8   8
#define ISDB_T_LA_CR_NOLAYER   16
	u8 layer_a_time_interleave;
#define ISDB_T_LA_TI_0  0
#define ISDB_T_LA_TI_1  1
#define ISDB_T_LA_TI_2  2
#define ISDB_T_LA_TI_4  4
#define ISDB_T_LA_TI_8  8
#define ISDB_T_LA_TI_16 16
#define ISDB_T_LA_TI_32 32
	u8 layer_a_nseg;

	u8 layer_b_mode;
#define ISDB_T_LB_MODE_1 0
#define ISDB_T_LB_MODE_2 1
#define ISDB_T_LB_MODE_3 2
	u8 layer_b_carrier_modulation;
#define ISDB_T_LB_CM_DQPSK 0
#define ISDB_T_LB_CM_QPSK  1
#define ISDB_T_LB_CM_16QAM 2
#define ISDB_T_LB_CM_64QAM 3
#define ISDB_T_LB_CM_NOLAYER 4
	u8 layer_b_code_rate;
#define ISDB_T_LB_CR_1_2   0
#define ISDB_T_LB_CR_2_3   1
#define ISDB_T_LB_CR_3_4   2
#define ISDB_T_LB_CR_5_6   4
#define ISDB_T_LB_CR_7_8   8
#define ISDB_T_LB_CR_NOLAYER   16
	u8 layer_b_time_interleave;
#define ISDB_T_LB_TI_0  0
#define ISDB_T_LB_TI_1  1
#define ISDB_T_LB_TI_2  2
#define ISDB_T_LB_TI_4  4
#define ISDB_T_LB_TI_8  8
#define ISDB_T_LB_TI_16 16
#define ISDB_T_LB_TI_32 32
	u8 layer_b_nseg;

	u8 layer_c_mode;
#define ISDB_T_LC_MODE_1 0
#define ISDB_T_LC_MODE_2 1
#define ISDB_T_LC_MODE_3 2
	u8 layer_c_carrier_modulation;
#define ISDB_T_LC_CM_DQPSK 0
#define ISDB_T_LC_CM_QPSK  1
#define ISDB_T_LC_CM_16QAM 2
#define ISDB_T_LC_CM_64QAM 3
#define ISDB_T_LC_CM_NOLAYER 4
	u8 layer_c_code_rate;
#define ISDB_T_LC_CR_1_2   0
#define ISDB_T_LC_CR_2_3   1
#define ISDB_T_LC_CR_3_4   2
#define ISDB_T_LC_CR_5_6   4
#define ISDB_T_LC_CR_7_8   8
#define ISDB_T_LC_CR_NOLAYER   16
	u8 layer_c_time_interleave;
#define ISDB_T_LC_TI_0  0
#define ISDB_T_LC_TI_1  1
#define ISDB_T_LC_TI_2  2
#define ISDB_T_LC_TI_4  4
#define ISDB_T_LC_TI_8  8
#define ISDB_T_LC_TI_16 16
#define ISDB_T_LC_TI_32 32
	u8 layer_c_nseg;
};

int s921_isdb_cmd(struct s921_isdb_t *dev, u32 cmd, void *data);
#endif
