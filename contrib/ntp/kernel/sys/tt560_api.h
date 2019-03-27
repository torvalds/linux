/***********************************************************************
 *
 *  Module:  ttime_api.h
 *
 *  Author:  SIS  1998
 *           LM NE&SS 2001
 *
 *  Description
 *
 *      This header file contains data necessary for the API to the
 *      True Time board. This contains all of the structure definitions
 *      for the individual registers.
 *
 ***********************************************************************/
#ifndef TTIME_API_H
#define TTIME_API_H

#ifdef CPP
extern "C" {
#endif

#include <time.h>

typedef struct
{
    unsigned int	micro_sec;
    unsigned int	milli_sec;
    struct tm		gps_tm;
} gps_time_t;

typedef struct 
{
    unsigned char reserved_1;
    unsigned unit_ms		: 4;
    unsigned filler_0		: 4;
    unsigned hun_ms		: 4;
    unsigned tens_ms		: 4;
    unsigned tens_sec		: 4;
    unsigned unit_sec		: 4;

    unsigned tens_min		: 4;
    unsigned unit_min		: 4;
    unsigned tens_hour		: 4;
    unsigned unit_hour		: 4;
    unsigned tens_day		: 4;
    unsigned unit_day		: 4;
    unsigned filler_1		: 4;
    unsigned hun_day		: 4;

    unsigned tens_year		: 4;
    unsigned unit_year		: 4;
    unsigned thou_year		: 4;
    unsigned hun_year		: 4;
    unsigned char reserved_2[2];
} preset_time_reg_t;

typedef struct
{
    unsigned n_d0			: 2;
    unsigned antenna_short_stat		: 1; /* 0 = fault */
    unsigned antenna_open_stat		: 1; /* 0 = fault */
    unsigned n_d1			: 1;
    unsigned rate_gen_pulse_stat	: 1;
    unsigned time_cmp_pulse_stat	: 1;
    unsigned ext_event_stat		: 1;

} hw_stat_reg_t;

typedef struct 
{
    unsigned tens_us		: 4;
    unsigned unit_us		: 4;
    unsigned unit_ms		: 4;
    unsigned hun_us		: 4;
    unsigned char hw_stat;	/* hw_stat_reg_t hw_stat; */
    unsigned char reserved_3;

    unsigned hun_ms		: 4;
    unsigned tens_ms		: 4;
    unsigned tens_sec		: 4;
    unsigned unit_sec		: 4;
    unsigned tens_min		: 4;
    unsigned unit_min		: 4;
    unsigned tens_hour		: 4;
    unsigned unit_hour		: 4;

    unsigned tens_day		: 4;
    unsigned unit_day		: 4;
    unsigned status		: 4;
    unsigned hun_day		: 4;
    unsigned tens_year		: 4;
    unsigned unit_year		: 4;
    unsigned thou_year		: 4;
    unsigned hun_year		: 4;
} time_freeze_reg_t;

typedef struct
{
    unsigned char off_low;
    unsigned char off_high;
    unsigned char reserved_4[2];
} sync_gen_off_reg_t;

typedef struct
{
    unsigned tens_min		: 4;
    unsigned unit_min		: 4;
    unsigned tens_hour		: 4;
    unsigned unit_hour		: 4;
    unsigned char sign_ascii;	/* '+' or '-' */
    unsigned char reserved_5;
} local_off_t;

/*
 *  This structure can be used for both the position freeze
 *  and position preset registers.
 */
typedef struct
{
    unsigned lat_tens_degee		: 4;
    unsigned lat_unit_degee		: 4;
    unsigned filler_0			: 4;
    unsigned lat_hun_degree		: 4;
    unsigned lat_tens_min		: 4;
    unsigned lat_unit_min		: 4;
    unsigned char lat_north_south;	/* 'N' or 'S' */

    unsigned filler_1			: 4;
    unsigned lat_tenth_sec		: 4;
    unsigned lat_tens_sec		: 4;
    unsigned lat_unit_sec		: 4;
    unsigned long_tens_degree		: 4;
    unsigned long_unit_degree		: 4;
    unsigned filler_2			: 4;
    unsigned long_hun_degree		: 4;

    unsigned long_tens_min		: 4;
    unsigned long_unit_min		: 4;
    unsigned char long_east_west;	/* 'E' or 'W' */
    unsigned filler_3			: 4;
    unsigned long_tenth_sec		: 4;
    unsigned long_tens_sec		: 4;
    unsigned long_unit_sec		: 4;

    unsigned elv_tens_km		: 4;
    unsigned elv_unit_km		: 4;
    unsigned char elv_sign;		/* '+' or '-' */
    unsigned elv_unit_m			: 4;
    unsigned elv_tenth_m		: 4;
    unsigned elv_hun_m			: 4;
    unsigned elv_tens_m			: 4;
} pos_reg_t;

typedef struct
{
    unsigned char prn1_tens_units;
    unsigned char prn1_reserved;
    unsigned char lvl1_tenths_hundredths;
    unsigned char lvl1_tens_units;

    unsigned char prn2_tens_units;
    unsigned char prn2_reserved;
    unsigned char lvl2_tenths_hundredths;
    unsigned char lvl2_tens_units;

    unsigned char prn3_tens_units;
    unsigned char prn3_reserved;
    unsigned char lvl3_tenths_hundredths;
    unsigned char lvl3_tens_units;

    unsigned char prn4_tens_units;
    unsigned char prn4_reserved;
    unsigned char lvl4_tenths_hundredths;
    unsigned char lvl4_tens_units;

    unsigned char prn5_tens_units;
    unsigned char prn5_reserved;
    unsigned char lvl5_tenths_hundredths;
    unsigned char lvl5_tens_units;

    unsigned char prn6_tens_units;
    unsigned char prn6_reserved;
    unsigned char lvl6_tenths_hundredths;
    unsigned char lvl6_tens_units;

    unsigned char flag;
    unsigned char reserved[3];
} sig_levels_t;

typedef struct
{
    unsigned tens_us		: 4;
    unsigned unit_us		: 4;
    unsigned unit_ms		: 4;
    unsigned hun_us		: 4;
    unsigned hun_ms		: 4;
    unsigned tens_ms		: 4;
    unsigned tens_sec		: 4;
    unsigned unit_sec		: 4;

    unsigned tens_min		: 4;
    unsigned unit_min		: 4;
    unsigned tens_hour		: 4;
    unsigned unit_hour		: 4;
    unsigned tens_day		: 4;
    unsigned unit_day		: 4;
    unsigned stat		: 4;
    unsigned hun_day		: 4;

    unsigned tens_year		: 4;
    unsigned unit_year		: 4;
    unsigned thou_year		: 4;
    unsigned hun_year		: 4;
    unsigned char reserved_5[2]; 
} ext_time_event_reg_t;

typedef struct
{
    unsigned tens_us	: 4;
    unsigned unit_us	: 4;
    unsigned unit_ms	: 4;
    unsigned hun_us	: 4;
    unsigned hun_ms	: 4;
    unsigned tens_ms	: 4;
    unsigned tens_sec	: 4;
    unsigned unit_sec	: 4;

    unsigned tens_min	: 4;
    unsigned unit_min	: 4;
    unsigned tens_hour	: 4;
    unsigned unit_hour	: 4;
    unsigned tens_day	: 4;
    unsigned unit_day	: 4;
    unsigned mask	: 4;
    unsigned hun_day	: 4;
} time_cmp_reg_t;

typedef struct
{
    unsigned char err_stat;
    unsigned char no_def;
    unsigned char oscillator_stat[2];
} diag_reg_t;

typedef struct
{
    unsigned res		:2;
    unsigned rate_int_mask	:1;
    unsigned cmp_int_mask	:1;
    unsigned ext_int_mask	:1;
    unsigned rate_stat_clr	:1;
    unsigned cmp_stat_clr	:1;
    unsigned ext_stat_clr	:1;
    unsigned char reserved[3];
} hw_ctl_reg_t;

typedef struct
{
    unsigned  preset_pos_rdy	:1;
    unsigned  sel_pps_ref	:1;
    unsigned  sel_gps_ref	:1;
    unsigned  sel_time_code	:1;
    unsigned  gen_stp_run	:1;
    unsigned  preset_time_rdy	:1;
    unsigned  dst		:1;
    unsigned  mode_sel		:1;

    unsigned  ctl_am_dc		:1;
    unsigned  reserved		:3;
    unsigned  input_code	:4;

    unsigned char rate_reserved;

    unsigned  rate_flag		:4;
    unsigned  rate_reserved1	:4;
} conf_reg_t;

typedef struct
{
     unsigned char	mem_reserved[0xf8];

     hw_ctl_reg_t	hw_ctl_reg;

     time_freeze_reg_t  time_freeze_reg;

     pos_reg_t		pos_freeze_reg;

     conf_reg_t		conf_reg;

     diag_reg_t		diag_reg;

     local_off_t	local_offset;

     sync_gen_off_reg_t sync_gen_offset;

     unsigned char 	reserved[4];

     unsigned char 	config_reg2_ctl;

     unsigned char      reserved2[11];

     time_cmp_reg_t	time_compare_reg;

     unsigned char 	reserved3[24];

     preset_time_reg_t	preset_time_reg;

     pos_reg_t		preset_pos_reg;

     ext_time_event_reg_t extern_time_event_reg;

     unsigned char 	reserved4[24];

     sig_levels_t	signal_levels_reg;

     unsigned char	reserved5[12];
} tt_mem_space_t;

#define TTIME_MEMORY_SIZE 0x200

/*
 *  Defines for register offsets
 */
#define HW_CTL_REG		0x0f8
#define TIME_FREEZE_REG		0x0fc
#define HW_STAT_REG		0x0fe
#define POS_FREEZE_REG		0x108
#define CONFIG_REG_1		0x118
#define DIAG_REG		0x11c
#define LOCAL_OFF_REG		0x120
#define SYNC_GEN_OFF_REG	0x124
#define CONFIG_REG_2		0x12c
#define TIME_CMP_REG		0x138
#define PRESET_TIME_REG		0x158
#define PRESET_POS_REG		0x164
#define EXT_EVENT_REG		0x174
#define SIG_LVL_PRN1		0x198
#define SIG_LVL_PRN2		0x19c
#define SIG_LVL_PRN3		0x1a0
#define SIG_LVL_PRN4		0x1a4
#define SIG_LVL_PRN5		0x1a8
#define SIG_LVL_PRN6		0x1ac
#define SIG_LVL_FLAG		0x1b0

/*
 *  Defines for accessing the hardware status register.
 */
#define HW_STAT_ANTENNA_SHORT		0	/* access the antenna short bit */
#define HW_STAT_ANTENNA_OPEN		1	/* access the antenna open bit */
#define HW_STAT_RATE_GEN_PULSE_STAT	2	/* access the rate gen pulse bit */
#define HW_STAT_TIME_CMP_PULSE_STAT	3	/* access the time cmp bit */
#define HW_STAT_EXT_EVENT_STAT		4	/* access the external event bit */

/*
 *  Defines for accessing the hardware control register
 */

#define HW_CTL_RATE_INT_MASK		0	/* access rate generator int mask */
#define HW_CTL_CMP_INT_MASK		1	/* access compare interrupt mask */
#define HW_CTL_EXT_INT_MASK		2	/* access external event interrupt mask */
#define HW_CTL_RATE_GEN_INT_CLEAR	3	/* access rate gen. interrupt clear field */
#define HW_CTL_TIME_CMP_INT_CLEAR	4	/* access time cmp interrupt clear field */
#define HW_CTL_EXT_EVENT_INT_CLEAR	5	/* access external event int clear field */

/*
 *  Defines for configuration register bit fields.
 */
#define PRESET_POS_RDY_BIT		0	/* access the preset pos. rdy. bit */
#define SEL_1_PPS_REF_BIT		1	/* access the select 1 pps reference bit */
#define SEL_GPS_REF_BIT			2	/* access the select gps reference bit */
#define SEL_TIME_CODE_REF_BIT		3	/* access the select time code reference bit */
#define GEN_STOP_BIT			4	/* access the generator start/stop bit */
#define PRESET_TIME_RDY_BIT		5	/* access the preset time ready bit */
#define DST_BIT				6	/* access the DST bit */
#define MODE_SEL_BIT			7	/* access the mode select bit */
#define AM_DC_BIT			8	/* access the code bits AM/DC bit */
#define IN_CODE_SEL_BIT			9	/* access the input code select bit */
#define FLAG_BIT			10	/* access the flag bit */

/*
 * The following defines are used to set modes in the 
 * configuration register.
 */

#define CONF_SET_AM			0	/* Set code to AM   */
#define CONF_SET_DC			1	/* Set code to DC   */
#define CONF_SET_IRIG_B			0	/* Set code IRIG B  */
#define CONF_SET_IRIG_A			1	/* Set code IRIG A  */

#define CONF_FLAG_DISABLE		0	/* Disable pulse        */
#define CONF_FLAG_10K_PPS		1	/* Set rate to 10k PPS  */
#define CONF_FLAG_1K_PPS		2	/* Set rate to 1k PPS   */
#define CONF_FLAG_100_PPS		3	/* Set rate to 100 PPS  */
#define CONF_FLAG_10_PPS		4	/* Set rate to 10 PPS   */
#define CONF_FLAG_1_PPS			5	/* Set rate to 1 PPS    */

/*
 *  Defines for read commands
 */

#define TT_RD_FREEZE_REG	0x01
#define TT_RD_HW_CTL_REG	0x02
#define TT_RD_CNFG_REG		0x03
#define TT_RD_DIAG_REG		0x04
#define TT_RD_LCL_OFFSET	0x05
#define TT_RD_SYNC_GEN_OFF	0x06
#define TT_RD_CNFG_REG_2	0x07
#define TT_RD_TIME_CMP_REG	0x08
#define TT_RD_PRESET_REG	0x09
#define TT_RD_EXT_EVNT_REG	0x0a
#define TT_RD_SIG_LVL_REG	0x0b

/*
 *  Defines for write commands
 */
#define TT_WRT_FREEZE_REG	0x0c
#define TT_WRT_HW_CTL_REG	0x0d
#define TT_WRT_CNFG_REG		0x0e
#define TT_WRT_DIAG_REG		0x0f
#define TT_WRT_LCL_OFFSET	0x10
#define TT_WRT_SYNC_GEN_OFF	0x11
#define TT_WRT_CNFG_REG_2	0x12
#define TT_WRT_TIME_CMP_REG	0x13
#define TT_WRT_PRESET_REG	0x14
#define TT_WRT_EXT_EVNT_REG	0x15
#define TT_WRT_SIG_LVL_REG	0x16

/*
 *  Define the length of the buffers to move (in 32 bit words).
 */

#define HW_CTL_REG_LEN		1
#define CNFG_REG_1_LEN		1
#define DIAG_REG_LEN		1
#define LCL_OFFSET_LEN		1
#define SYNC_GEN_OFF_LEN	1
#define CNFG_REG_2_LEN		1

#define TIME_CMP_REG_LEN	2
#define PRESET_TIME_REG_LEN	3
#define PRESET_POS_REG_LEN	4
#define PRESET_REG_LEN		(PRESET_TIME_REG_LEN+PRESET_POS_REG_LEN)
#define TIME_FREEZE_REG_LEN	3
#define POSN_FREEZE_REG_LEN	4
#define FREEZE_REG_LEN		(TIME_FREEZE_REG_LEN+POSN_FREEZE_REG_LEN)
#define EXT_EVNT_REG_LEN	3
#define	SIG_LVL_REG_LEN		7
#define	GPS_TIME_LEN		7

/*
 * Define BCD - INT - BCD macros.
 */

#define BCDTOI(a)	( ( ( ( (a) & 0xf0 ) >> 4 ) * 10 )  + ( (a) & 0x0f ) )
#define ITOBCD(a)	( ( ( (           (a) ) / 10) << 4 ) + ( (           (a) ) % 10) )
#define LTOBCD(a)	( ( ( ( (uint64_t)(a) ) / 10) << 4 ) + ( ( (uint64_t)(a) ) % 10) )

extern int  init_560                  (                      );
extern void close_560                 (                      );
extern int  write_hw_ctl_reg          (hw_ctl_reg_t         *);
extern int  write_hw_ctl_reg_bitfield (int, int              );
extern int  read_conf_reg             (conf_reg_t           *);
extern int  read_conf_reg_bitfield    (int                   );
extern int  write_conf_reg            (conf_reg_t           *);
extern int  write_conf_reg_bitfield   (int, unsigned char    );
extern int  read_hw_stat_reg_bitfield (int                   );
extern int  read_local_offset_reg     (local_off_t          *);
extern int  write_local_offset_reg    (local_off_t          *);
extern int  read_sync_offset_reg      (sync_gen_off_reg_t   *);
extern int  write_sync_offset_reg     (sync_gen_off_reg_t   *);
extern int  read_time_cmp_reg         (time_cmp_reg_t       *);
extern int  write_time_cmp_reg        (time_cmp_reg_t       *);
extern int  read_preset_time_reg      (preset_time_reg_t    *);
extern int  write_preset_time_reg     (preset_time_reg_t    *);
extern int  reset_time                (                      );
extern int  set_new_time              (preset_time_reg_t    *);
extern int  read_preset_position_reg  (pos_reg_t            *);
extern int  write_preset_position_reg (pos_reg_t            *);
extern int  read_external_event_reg   (ext_time_event_reg_t *);
extern int  read_signal_level_reg     (sig_levels_t         *);
extern int  freeze_time               (                      );
extern int  snapshot_time             (time_freeze_reg_t    *);
extern int  read_position_freeze_reg  (pos_reg_t            *);
extern int  read_diag_reg             (diag_reg_t           *);

#ifdef CPP
}
#endif
#endif
