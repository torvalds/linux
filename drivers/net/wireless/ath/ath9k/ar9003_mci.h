/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef AR9003_MCI_H
#define AR9003_MCI_H

#define MCI_FLAG_DISABLE_TIMESTAMP      0x00000001      /* Disable time stamp */
#define MCI_RECOVERY_DUR_TSF		(100 * 1000)    /* 100 ms */

/* Default remote BT device MCI COEX version */
#define MCI_GPM_COEX_MAJOR_VERSION_DEFAULT  3
#define MCI_GPM_COEX_MINOR_VERSION_DEFAULT  0

/* Local WLAN MCI COEX version */
#define MCI_GPM_COEX_MAJOR_VERSION_WLAN     3
#define MCI_GPM_COEX_MINOR_VERSION_WLAN     0

enum mci_gpm_coex_query_type {
	MCI_GPM_COEX_QUERY_BT_ALL_INFO      = BIT(0),
	MCI_GPM_COEX_QUERY_BT_TOPOLOGY      = BIT(1),
	MCI_GPM_COEX_QUERY_BT_DEBUG         = BIT(2),
};

enum mci_gpm_coex_halt_bt_gpm {
	MCI_GPM_COEX_BT_GPM_UNHALT,
	MCI_GPM_COEX_BT_GPM_HALT
};

enum mci_gpm_coex_bt_update_flags_op {
	MCI_GPM_COEX_BT_FLAGS_READ,
	MCI_GPM_COEX_BT_FLAGS_SET,
	MCI_GPM_COEX_BT_FLAGS_CLEAR
};

#define MCI_NUM_BT_CHANNELS     79

#define MCI_BT_MCI_FLAGS_UPDATE_CORR          0x00000002
#define MCI_BT_MCI_FLAGS_UPDATE_HDR           0x00000004
#define MCI_BT_MCI_FLAGS_UPDATE_PLD           0x00000008
#define MCI_BT_MCI_FLAGS_LNA_CTRL             0x00000010
#define MCI_BT_MCI_FLAGS_DEBUG                0x00000020
#define MCI_BT_MCI_FLAGS_SCHED_MSG            0x00000040
#define MCI_BT_MCI_FLAGS_CONT_MSG             0x00000080
#define MCI_BT_MCI_FLAGS_COEX_GPM             0x00000100
#define MCI_BT_MCI_FLAGS_CPU_INT_MSG          0x00000200
#define MCI_BT_MCI_FLAGS_MCI_MODE             0x00000400
#define MCI_BT_MCI_FLAGS_AR9462_MODE          0x00001000
#define MCI_BT_MCI_FLAGS_OTHER                0x00010000

#define MCI_DEFAULT_BT_MCI_FLAGS              0x00011dde

#define MCI_TOGGLE_BT_MCI_FLAGS  (MCI_BT_MCI_FLAGS_UPDATE_CORR | \
				  MCI_BT_MCI_FLAGS_UPDATE_HDR  | \
				  MCI_BT_MCI_FLAGS_UPDATE_PLD  | \
				  MCI_BT_MCI_FLAGS_MCI_MODE)

#define MCI_2G_FLAGS_CLEAR_MASK   0x00000000
#define MCI_2G_FLAGS_SET_MASK     MCI_TOGGLE_BT_MCI_FLAGS
#define MCI_2G_FLAGS              MCI_DEFAULT_BT_MCI_FLAGS

#define MCI_5G_FLAGS_CLEAR_MASK   MCI_TOGGLE_BT_MCI_FLAGS
#define MCI_5G_FLAGS_SET_MASK     0x00000000
#define MCI_5G_FLAGS              (MCI_DEFAULT_BT_MCI_FLAGS & \
				   ~MCI_TOGGLE_BT_MCI_FLAGS)

/*
 * Default value for AR9462 is 0x00002201
 */
#define ATH_MCI_CONFIG_CONCUR_TX            0x00000003
#define ATH_MCI_CONFIG_MCI_OBS_MCI          0x00000004
#define ATH_MCI_CONFIG_MCI_OBS_TXRX         0x00000008
#define ATH_MCI_CONFIG_MCI_OBS_BT           0x00000010
#define ATH_MCI_CONFIG_DISABLE_MCI_CAL      0x00000020
#define ATH_MCI_CONFIG_DISABLE_OSLA         0x00000040
#define ATH_MCI_CONFIG_DISABLE_FTP_STOMP    0x00000080
#define ATH_MCI_CONFIG_AGGR_THRESH          0x00000700
#define ATH_MCI_CONFIG_AGGR_THRESH_S        8
#define ATH_MCI_CONFIG_DISABLE_AGGR_THRESH  0x00000800
#define ATH_MCI_CONFIG_CLK_DIV              0x00003000
#define ATH_MCI_CONFIG_CLK_DIV_S            12
#define ATH_MCI_CONFIG_DISABLE_TUNING       0x00004000
#define ATH_MCI_CONFIG_DISABLE_AIC          0x00008000
#define ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN     0x007f0000
#define ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN_S   16
#define ATH_MCI_CONFIG_NO_QUIET_ACK         0x00800000
#define ATH_MCI_CONFIG_NO_QUIET_ACK_S       23
#define ATH_MCI_CONFIG_ANT_ARCH             0x07000000
#define ATH_MCI_CONFIG_ANT_ARCH_S           24
#define ATH_MCI_CONFIG_FORCE_QUIET_ACK      0x08000000
#define ATH_MCI_CONFIG_FORCE_QUIET_ACK_S    27
#define ATH_MCI_CONFIG_FORCE_2CHAIN_ACK     0x10000000
#define ATH_MCI_CONFIG_MCI_STAT_DBG         0x20000000
#define ATH_MCI_CONFIG_MCI_WEIGHT_DBG       0x40000000
#define ATH_MCI_CONFIG_DISABLE_MCI          0x80000000

#define ATH_MCI_CONFIG_MCI_OBS_MASK     (ATH_MCI_CONFIG_MCI_OBS_MCI  | \
					 ATH_MCI_CONFIG_MCI_OBS_TXRX | \
					 ATH_MCI_CONFIG_MCI_OBS_BT)

#define ATH_MCI_CONFIG_MCI_OBS_GPIO     0x0000002F

#define ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_NON_SHARED 0x00
#define ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED     0x01
#define ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_NON_SHARED 0x02
#define ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_SHARED     0x03
#define ATH_MCI_ANT_ARCH_3_ANT                   0x04

#define MCI_ANT_ARCH_PA_LNA_SHARED(mci)					\
	((MS(mci->config, ATH_MCI_CONFIG_ANT_ARCH) == ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED) || \
	 (MS(mci->config, ATH_MCI_CONFIG_ANT_ARCH) == ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_SHARED))

enum mci_message_header {		/* length of payload */
	MCI_LNA_CTRL     = 0x10,        /* len = 0 */
	MCI_CONT_NACK    = 0x20,        /* len = 0 */
	MCI_CONT_INFO    = 0x30,        /* len = 4 */
	MCI_CONT_RST     = 0x40,        /* len = 0 */
	MCI_SCHD_INFO    = 0x50,        /* len = 16 */
	MCI_CPU_INT      = 0x60,        /* len = 4 */
	MCI_SYS_WAKING   = 0x70,        /* len = 0 */
	MCI_GPM          = 0x80,        /* len = 16 */
	MCI_LNA_INFO     = 0x90,        /* len = 1 */
	MCI_LNA_STATE    = 0x94,
	MCI_LNA_TAKE     = 0x98,
	MCI_LNA_TRANS    = 0x9c,
	MCI_SYS_SLEEPING = 0xa0,        /* len = 0 */
	MCI_REQ_WAKE     = 0xc0,        /* len = 0 */
	MCI_DEBUG_16     = 0xfe,        /* len = 2 */
	MCI_REMOTE_RESET = 0xff         /* len = 16 */
};

enum ath_mci_gpm_coex_profile_type {
	MCI_GPM_COEX_PROFILE_UNKNOWN,
	MCI_GPM_COEX_PROFILE_RFCOMM,
	MCI_GPM_COEX_PROFILE_A2DP,
	MCI_GPM_COEX_PROFILE_HID,
	MCI_GPM_COEX_PROFILE_BNEP,
	MCI_GPM_COEX_PROFILE_VOICE,
	MCI_GPM_COEX_PROFILE_A2DPVO,
	MCI_GPM_COEX_PROFILE_MAX
};

/* MCI GPM/Coex opcode/type definitions */
enum {
	MCI_GPM_COEX_W_GPM_PAYLOAD      = 1,
	MCI_GPM_COEX_B_GPM_TYPE         = 4,
	MCI_GPM_COEX_B_GPM_OPCODE       = 5,
	/* MCI_GPM_WLAN_CAL_REQ, MCI_GPM_WLAN_CAL_DONE */
	MCI_GPM_WLAN_CAL_W_SEQUENCE     = 2,

	/* MCI_GPM_COEX_VERSION_QUERY */
	/* MCI_GPM_COEX_VERSION_RESPONSE */
	MCI_GPM_COEX_B_MAJOR_VERSION    = 6,
	MCI_GPM_COEX_B_MINOR_VERSION    = 7,
	/* MCI_GPM_COEX_STATUS_QUERY */
	MCI_GPM_COEX_B_BT_BITMAP        = 6,
	MCI_GPM_COEX_B_WLAN_BITMAP      = 7,
	/* MCI_GPM_COEX_HALT_BT_GPM */
	MCI_GPM_COEX_B_HALT_STATE       = 6,
	/* MCI_GPM_COEX_WLAN_CHANNELS */
	MCI_GPM_COEX_B_CHANNEL_MAP      = 6,
	/* MCI_GPM_COEX_BT_PROFILE_INFO */
	MCI_GPM_COEX_B_PROFILE_TYPE     = 6,
	MCI_GPM_COEX_B_PROFILE_LINKID   = 7,
	MCI_GPM_COEX_B_PROFILE_STATE    = 8,
	MCI_GPM_COEX_B_PROFILE_ROLE     = 9,
	MCI_GPM_COEX_B_PROFILE_RATE     = 10,
	MCI_GPM_COEX_B_PROFILE_VOTYPE   = 11,
	MCI_GPM_COEX_H_PROFILE_T        = 12,
	MCI_GPM_COEX_B_PROFILE_W        = 14,
	MCI_GPM_COEX_B_PROFILE_A        = 15,
	/* MCI_GPM_COEX_BT_STATUS_UPDATE */
	MCI_GPM_COEX_B_STATUS_TYPE      = 6,
	MCI_GPM_COEX_B_STATUS_LINKID    = 7,
	MCI_GPM_COEX_B_STATUS_STATE     = 8,
	/* MCI_GPM_COEX_BT_UPDATE_FLAGS */
	MCI_GPM_COEX_W_BT_FLAGS         = 6,
	MCI_GPM_COEX_B_BT_FLAGS_OP      = 10
};

enum mci_gpm_subtype {
	MCI_GPM_BT_CAL_REQ      = 0,
	MCI_GPM_BT_CAL_GRANT    = 1,
	MCI_GPM_BT_CAL_DONE     = 2,
	MCI_GPM_WLAN_CAL_REQ    = 3,
	MCI_GPM_WLAN_CAL_GRANT  = 4,
	MCI_GPM_WLAN_CAL_DONE   = 5,
	MCI_GPM_COEX_AGENT      = 0x0c,
	MCI_GPM_RSVD_PATTERN    = 0xfe,
	MCI_GPM_RSVD_PATTERN32  = 0xfefefefe,
	MCI_GPM_BT_DEBUG        = 0xff
};

enum mci_bt_state {
	MCI_BT_SLEEP,
	MCI_BT_AWAKE,
	MCI_BT_CAL_START,
	MCI_BT_CAL
};

enum mci_ps_state {
	MCI_PS_DISABLE,
	MCI_PS_ENABLE,
	MCI_PS_ENABLE_OFF,
	MCI_PS_ENABLE_ON
};

/* Type of state query */
enum mci_state_type {
	MCI_STATE_ENABLE,
	MCI_STATE_INIT_GPM_OFFSET,
	MCI_STATE_CHECK_GPM_OFFSET,
	MCI_STATE_NEXT_GPM_OFFSET,
	MCI_STATE_LAST_GPM_OFFSET,
	MCI_STATE_BT,
	MCI_STATE_SET_BT_SLEEP,
	MCI_STATE_SET_BT_AWAKE,
	MCI_STATE_SET_BT_CAL_START,
	MCI_STATE_SET_BT_CAL,
	MCI_STATE_LAST_SCHD_MSG_OFFSET,
	MCI_STATE_REMOTE_SLEEP,
	MCI_STATE_CONT_STATUS,
	MCI_STATE_RESET_REQ_WAKE,
	MCI_STATE_SEND_WLAN_COEX_VERSION,
	MCI_STATE_SET_BT_COEX_VERSION,
	MCI_STATE_SEND_WLAN_CHANNELS,
	MCI_STATE_SEND_VERSION_QUERY,
	MCI_STATE_SEND_STATUS_QUERY,
	MCI_STATE_NEED_FLUSH_BT_INFO,
	MCI_STATE_SET_CONCUR_TX_PRI,
	MCI_STATE_RECOVER_RX,
	MCI_STATE_NEED_FTP_STOMP,
	MCI_STATE_NEED_TUNING,
	MCI_STATE_NEED_STAT_DEBUG,
	MCI_STATE_SHARED_CHAIN_CONCUR_TX,
	MCI_STATE_AIC_CAL,
	MCI_STATE_AIC_START,
	MCI_STATE_AIC_CAL_RESET,
	MCI_STATE_AIC_CAL_SINGLE,
	MCI_STATE_IS_AR9462,
	MCI_STATE_IS_AR9565_1ANT,
	MCI_STATE_IS_AR9565_2ANT,
	MCI_STATE_WLAN_WEAK_SIGNAL,
	MCI_STATE_SET_WLAN_PS_STATE,
	MCI_STATE_GET_WLAN_PS_STATE,
	MCI_STATE_DEBUG,
	MCI_STATE_STAT_DEBUG,
	MCI_STATE_ALLOW_FCS,
	MCI_STATE_SET_2G_CONTENTION,
	MCI_STATE_MAX
};

enum mci_gpm_coex_opcode {
	MCI_GPM_COEX_VERSION_QUERY,
	MCI_GPM_COEX_VERSION_RESPONSE,
	MCI_GPM_COEX_STATUS_QUERY,
	MCI_GPM_COEX_HALT_BT_GPM,
	MCI_GPM_COEX_WLAN_CHANNELS,
	MCI_GPM_COEX_BT_PROFILE_INFO,
	MCI_GPM_COEX_BT_STATUS_UPDATE,
	MCI_GPM_COEX_BT_UPDATE_FLAGS,
	MCI_GPM_COEX_NOOP,
};

#define MCI_GPM_NOMORE  0
#define MCI_GPM_MORE    1
#define MCI_GPM_INVALID 0xffffffff

#define MCI_GPM_RECYCLE(_p_gpm)	do {			  \
	*(((u32 *)_p_gpm) + MCI_GPM_COEX_W_GPM_PAYLOAD) = \
				MCI_GPM_RSVD_PATTERN32;   \
} while (0)

#define MCI_GPM_TYPE(_p_gpm)	\
	(*(((u8 *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) & 0xff)

#define MCI_GPM_OPCODE(_p_gpm)	\
	(*(((u8 *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_OPCODE) & 0xff)

#define MCI_GPM_SET_CAL_TYPE(_p_gpm, _cal_type)	do {			   \
	*(((u8 *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) = (_cal_type) & 0xff;\
} while (0)

#define MCI_GPM_SET_TYPE_OPCODE(_p_gpm, _type, _opcode) do {		   \
	*(((u8 *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) = (_type) & 0xff;	   \
	*(((u8 *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_OPCODE) = (_opcode) & 0xff;\
} while (0)

#define MCI_GPM_IS_CAL_TYPE(_type) ((_type) <= MCI_GPM_WLAN_CAL_DONE)

/*
 * Functions that are available to the MCI driver core.
 */
bool ar9003_mci_send_message(struct ath_hw *ah, u8 header, u32 flag,
			     u32 *payload, u8 len, bool wait_done,
			     bool check_bt);
u32 ar9003_mci_state(struct ath_hw *ah, u32 state_type);
int ar9003_mci_setup(struct ath_hw *ah, u32 gpm_addr, void *gpm_buf,
		     u16 len, u32 sched_addr);
void ar9003_mci_cleanup(struct ath_hw *ah);
void ar9003_mci_get_interrupt(struct ath_hw *ah, u32 *raw_intr,
			      u32 *rx_msg_intr);
u32 ar9003_mci_get_next_gpm_offset(struct ath_hw *ah, u32 *more);
void ar9003_mci_set_bt_version(struct ath_hw *ah, u8 major, u8 minor);
void ar9003_mci_send_wlan_channels(struct ath_hw *ah);
/*
 * These functions are used by ath9k_hw.
 */

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT

void ar9003_mci_stop_bt(struct ath_hw *ah, bool save_fullsleep);
void ar9003_mci_init_cal_req(struct ath_hw *ah, bool *is_reusable);
void ar9003_mci_init_cal_done(struct ath_hw *ah);
void ar9003_mci_set_full_sleep(struct ath_hw *ah);
void ar9003_mci_2g5g_switch(struct ath_hw *ah, bool force);
void ar9003_mci_check_bt(struct ath_hw *ah);
bool ar9003_mci_start_reset(struct ath_hw *ah, struct ath9k_channel *chan);
int ar9003_mci_end_reset(struct ath_hw *ah, struct ath9k_channel *chan,
			 struct ath9k_hw_cal_data *caldata);
int ar9003_mci_reset(struct ath_hw *ah, bool en_int, bool is_2g,
		     bool is_full_sleep);
void ar9003_mci_get_isr(struct ath_hw *ah, enum ath9k_int *masked);
void ar9003_mci_bt_gain_ctrl(struct ath_hw *ah);
void ar9003_mci_set_power_awake(struct ath_hw *ah);
void ar9003_mci_check_gpm_offset(struct ath_hw *ah);
u16 ar9003_mci_get_max_txpower(struct ath_hw *ah, u8 ctlmode);

#else

static inline void ar9003_mci_stop_bt(struct ath_hw *ah, bool save_fullsleep)
{
}
static inline void ar9003_mci_init_cal_req(struct ath_hw *ah, bool *is_reusable)
{
}
static inline void ar9003_mci_init_cal_done(struct ath_hw *ah)
{
}
static inline void ar9003_mci_set_full_sleep(struct ath_hw *ah)
{
}
static inline void ar9003_mci_2g5g_switch(struct ath_hw *ah, bool wait_done)
{
}
static inline void ar9003_mci_check_bt(struct ath_hw *ah)
{
}
static inline bool ar9003_mci_start_reset(struct ath_hw *ah, struct ath9k_channel *chan)
{
	return false;
}
static inline int ar9003_mci_end_reset(struct ath_hw *ah, struct ath9k_channel *chan,
				       struct ath9k_hw_cal_data *caldata)
{
	return 0;
}
static inline void ar9003_mci_reset(struct ath_hw *ah, bool en_int, bool is_2g,
				    bool is_full_sleep)
{
}
static inline void ar9003_mci_get_isr(struct ath_hw *ah, enum ath9k_int *masked)
{
}
static inline void ar9003_mci_bt_gain_ctrl(struct ath_hw *ah)
{
}
static inline void ar9003_mci_set_power_awake(struct ath_hw *ah)
{
}
static inline void ar9003_mci_check_gpm_offset(struct ath_hw *ah)
{
}
static inline u16 ar9003_mci_get_max_txpower(struct ath_hw *ah, u8 ctlmode)
{
	return -1;
}
#endif /* CONFIG_ATH9K_BTCOEX_SUPPORT */

#endif
