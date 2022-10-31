#ifndef __ECRNX_DEBUGFS_FUNC_H_
#define __ECRNX_DEBUGFS_FUNC_H_

#include "ecrnx_defs.h"
#include "ecrnx_utils.h"
#include "eswin_utils.h"
#include "fw_head_check.h"

#include <linux/etherdevice.h>



#define HOST_DRIVER_VERSION     "1.0.0"

#define NX_VIRT_STA_MAX         (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)

typedef enum {
    IF_STA = 0,
    IF_AP,
    IF_P2P
} IF_TYPE_EN;

typedef struct {
    u8 ch;
    u8 bw;
    u32 ch_offset;
} RF_INFO_ST;

typedef struct {
    u8 name[48];
    u32 addr;
}REG_MAP_ST;

struct ecrnx_debugfs_survey_info_tbl
{
	struct list_head list;
	u8		ssid[IEEE80211_MAX_SSID_LEN];
	u8		bssid[ETH_ALEN];
	u32		ch;
	int		rssi;
	s32		sdbm;
	s16		noise;
	u32		age;
	char	flag[64];
};

struct ecrnx_debugfs_sta
{
	u8 mac_addr[ETH_ALEN];
    u16 aid;                /* association ID */
    u8 sta_idx;             /* Identifier of the station */
	enum nl80211_chan_width width; /* Channel width */
	u8 ch_idx;              /* Identifier of the channel
                               context the station belongs to */
	bool qos;               /* Flag indicating if the station
                               supports QoS */
    bool ht;               /* Flag indicating if the station
                               supports HT */
    bool vht;               /* Flag indicating if the station
                               supports VHT */

	bool sgi_20m;
	bool sgi_40m;

	u8	ampdu_enable;/* for enable Tx A-MPDU */
	
	/* for processing Tx A-MPDU */
	u8	agg_enable_bitmap;
	/* u8	ADDBA_retry_count; */
	u8	candidate_tid_bitmap;

	u8	ldpc_cap;
	u8	stbc_cap;
	u8	beamform_cap;
};

#define MAX_ARGV                10

typedef int (*_cmd_func)(uint32_t index, uint8_t *param);

typedef struct{
    uint32_t range_min;
    uint32_t range_max;
}ECRNX_CLI_PARAM_RANGE_ST;

typedef struct {
    char cmd[20];
    char map_cmd[20];
    uint32_t param_num;
    ECRNX_CLI_PARAM_RANGE_ST param_range[MAX_ARGV];
    _cmd_func cmd_func;
}ECRNX_CLI_TABLE_ST;

extern ECRNX_CLI_TABLE_ST ecrnx_cli_tab[];

enum debugfs_type
{
    SLAVE_LOG_LEVEL = 1,
    SLAVE_FW_INFO,
    SLAVE_RF_INFO,
    SLAVE_MAC_REG_DUMP,
    SLAVE_RF_REG_DUMP,
    SLAVE_BB_REG_DUMP,
    SLAVE_COUNTRY_CODE,
    SLAVE_EFUSE_MAP,
    SLAVE_WOW_ENABLE,
    SLAVE_WOW_WLAN_GOPI_INFO,
    SLAVE_READ_REG,
    SLAVE_WRITE_REG,
    SLAVE_FUNC_INFO,
    SLAVE_DEBUGFS_MAX,
};

typedef struct
{
    uint32_t debugfs_type;

    union
    {
        struct 
        {
            uint32_t debug_level;
            uint32_t debug_dir;
        }slave_log_level_t;

        struct
        {
            uint32_t info;
            uint32_t info_len;
        }slave_fw_info_t;

        struct
        {
            uint32_t reg;
            uint32_t len;
        }slave_read_reg_t;

        struct
        {
            uint32_t reg;
            uint32_t value;
        }slave_write_reg_t;

        struct
        {
            uint8_t func_and_param[56];
        }slave_cli_func_info_t;

    }u;
}debugfs_info_t;

typedef struct
{
    uint32_t debugfs_type;
    unsigned char   rxdata[ECRNX_RXSIZE];
    int             rxlen;
    wait_queue_head_t   rxdataq;
    int                 rxdatas;
}debugfs_resp_t;

extern debugfs_info_t debugfs_info;
extern debugfs_resp_t debugfs_resp;

extern u32 reg_buf[512];

void ecrnx_debug_param_send(dbg_req_t *req);

struct ring_buffer *usb_dbg_buf_get(void);

int ecrnx_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
                                      int idx, u8 *mac, struct station_info *sinfo);
int ecrnx_cfg80211_get_channel(struct wiphy *wiphy,
                                     struct wireless_dev *wdev,
                                     struct cfg80211_chan_def *chandef);
int ecrnx_log_level_get(LOG_CTL_ST *log);
int ecrnx_fw_log_level_set(u32 level, u32 dir);
bool ecrnx_log_host_enable(void);
int ecrnx_host_ver_get(u8 *ver);
int ecrnx_fw_ver_get(u8 *ver);
int ecrnx_build_time_get(u8 *build_time);
int ecrnx_rf_info_get(struct seq_file *seq, IF_TYPE_EN iftype,RF_INFO_ST *cur, RF_INFO_ST *oper);
int ecrnx_country_code_get(struct seq_file *seq, char *alpha2);
int ecrnx_mac_addr_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 *mac_addr);
int ecrnx_mac_addr_get_ex(struct seq_file *seq, u8 *mac_addr_info);
int ecrnx_channel_get(struct seq_file *seq, IF_TYPE_EN iftype, struct cfg80211_chan_def *chandef);
int ecrnx_p2p_role_get(struct seq_file *seq, IF_TYPE_EN iftype);
int ecrnx_bssid_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 *bssid);
int ecrnx_signal_level_get(struct seq_file *seq, IF_TYPE_EN iftype, s8 *noise_dbm);
int ecrnx_flags_get(struct seq_file *seq, IF_TYPE_EN iftype, u32 *flags);
int ecrnx_ssid_get(struct seq_file *seq, IF_TYPE_EN iftype, char *ssid);
int ecrnx_sta_mac_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 sta_mac[][ETH_ALEN+1]);
int ecrnx_mac_reg_dump(struct seq_file *seq);
int ecrnx_rf_reg_dump(struct seq_file *seq);
int ecrnx_bb_reg_dump(struct seq_file *seq);
int ecrnx_efuse_map_dump(struct seq_file *seq);
int ecrnx_slave_reg_read(u32 addr, u32 *data, u32 len);
int ecrnx_slave_reg_write(u32 addr, u32 data, u32 len);
int ecrnx_slave_cli_send(u8 *cli, u8 *resp);
void ecrnx_debugfs_survey_info_update(struct ecrnx_hw *ecrnx_hw, struct cfg80211_bss *bss);
void ecrnx_debugfs_noise_of_survey_info_update(struct ecrnx_hw *ecrnx_hw,
										struct ecrnx_survey_info *ecrnx_survey, int chan_no);
void ecrnx_debugfs_add_station_in_ap_mode(struct ecrnx_hw *ecrnx_hw,
											struct ecrnx_sta *sta, struct station_parameters *params);
int cli_macbyp_start(uint32_t index, uint8_t *param);
int cli_macbyp_stop(uint32_t index, uint8_t *param);
int cli_rf_txgain(uint32_t index, uint8_t *param);
int cli_rf_rxgain(uint32_t index, uint8_t *param);
int cli_rf_chan(uint32_t index, uint8_t *param);
int cli_cmd_parse(uint8_t *cmd);

#endif
