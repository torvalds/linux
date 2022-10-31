#ifndef _CORE_H_
#define _CORE_H_


#include <linux/completion.h>
#include <linux/if_ether.h>

#include <net/mac80211.h>
#include "ecrnx_compat.h"


#define ESWIN_FLAG_CORE_REGISTERED	1

#define ESWIN_NR_VIF		2
#define ESWIN_NR_VIF_HW_QUEUE	4


#define ESWIN_STATE_BOOT		1
#define ESWIN_STATE_INIT		2
#define ESWIN_STATE_STOP		3
#define ESWIN_STATE_CLOSING	4
#define ESWIN_STATE_CLOSEED	5
#define ESWIN_STATE_START	6
#define ESWIN_STATE_RUNNING	7


#define WIM_RESP_TIMEOUT    (msecs_to_jiffies(100))

enum ESWIN_SCAN_MODE {
	ESWIN_SCAN_MODE_IDLE = 0,
	ESWIN_SCAN_MODE_SCANNING,
	ESWIN_SCAN_MODE_ABORTING,
};



struct fwinfo_t {
	uint32_t ready;
	uint32_t version;
	uint32_t tx_head_size;
	uint32_t rx_head_size;
	uint32_t payload_align;
	uint32_t buffer_size;
};

struct eswin_capabilities {
	uint64_t cap_mask;
	uint16_t listen_interval;
	uint16_t bss_max_idle;
	uint8_t bss_max_idle_options;
};

struct eswin_max_idle {
	bool enable;
	u16 period;
	u16 scale_factor;
	u8 options;
	struct timer_list keep_alive_timer;

	unsigned long idle_period; /* jiffies */
	struct timer_list timer;
};


/* Private txq driver data structure */
struct eswin_txq {
	u16 hw_queue; /* 0: AC_BK, 1: AC_BE, 2: AC_VI, 3: AC_VO */
	struct list_head list;
	struct sk_buff_head queue; /* own queue */
	unsigned long nr_fw_queueud;
	unsigned long nr_push_allowed;
	struct ieee80211_vif vif;
	struct ieee80211_sta sta;
};

struct tx_buff_node {
	struct tx_buff_node * next;
	void * buff;
	int    len;
	int    flag;
};
struct tx_buff_queue {
	struct tx_buff_node * head;
	struct tx_buff_node * tail;
	int    count;
	spinlock_t lock;
};

#define ESWIN_QUEUE_MAX	 (ESWIN_NR_VIF_HW_QUEUE*ESWIN_NR_VIF + 3)

typedef int (*usb_rx_cb_t)(void *priv, struct sk_buff *skb, unsigned char ep);
typedef int (*usb_data_cfm_cb_t)(void *priv, void *host_id);
struct eswin {

    void *umac_priv; //mac drv data.
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif[ESWIN_NR_VIF];
	struct device *dev;
	int nr_active_vif;
	int state;
	bool promisc;

	bool loopback;
	bool ampdu_supported;
	int lb_count;
	bool amsdu_supported;
	bool block_frame;
	bool ampdu_reject;
	
	char alpha2[2];
	u64 tsf_offset;

	struct usb_ops *ops;
	usb_rx_cb_t rx_callback;
	usb_data_cfm_cb_t data_cfm_callback;
	usb_data_cfm_cb_t msg_cfm_callback;

	//struct sx_buff_queue queue[ESWIN_NR_VIF]; /* 0: frame, 1: wim */
	struct tx_buff_queue tx_queue;
	struct tx_buff_node tx_node[64];
	struct tx_buff_node * tx_node_head;
	spinlock_t tx_lock;
	int tx_node_num;
	struct work_struct work;

	struct eswin_txq ntxq[ESWIN_QUEUE_MAX];


	struct mutex state_mtx;
	enum ESWIN_SCAN_MODE scan_mode;
	struct workqueue_struct *workqueue;
	struct delayed_work scan_timeout;

	//struct work_struct register_work;
	struct delayed_work register_work;


	unsigned long dev_flags;
	struct mac_address mac_addr[ESWIN_NR_VIF];
	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];



	/* Move to vif or sta driver data */
	u8 frame_seqno;
	u8 wim_seqno;
	u8 band;
	u16 center_freq;
	u16 aid;
	u32 cipher_pairwise;
	u32 cipher_group;
	
	struct fwinfo_t fwinfo;
	struct eswin_capabilities cap;

	/* power management */
	enum ps_mode {
		PS_DISABLED,
		PS_ENABLED,
		PS_AUTO_POLL,
		PS_MANUAL_POLL
	} ps;
	bool ps_poll_pending;
	bool ps_enabled;



	/* tx */
	spinlock_t txq_lock;
	struct list_head txq;
	/* 0: AC_BK, 1: AC_BE, 2: AC_VI, 3: AC_VO */
	atomic_t tx_credit[IEEE80211_NUM_ACS*3];
	atomic_t tx_pend[IEEE80211_NUM_ACS*3];

//	struct completion wim_responded;
//	struct sk_buff *last_wim_responded;


	struct delayed_work roc_finish;

	struct firmware *fw;

	struct dentry *debugfs;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};


/* vif driver data structure */
struct eswin_vif {
	struct eswin *tr;
	int index;
	struct net_device *dev;

	/* scan */
	struct delayed_work scan_timeout;

	/* power save */
	bool ps_polling;

	/* MLME */
	spinlock_t preassoc_sta_lock;
	struct list_head preassoc_sta_list;

	/* inactivity */
	u16 max_idle_period;
};

#define to_ieee80211_vif(v) \
	container_of((void *)v, struct ieee80211_vif, drv_priv)

#define to_i_vif(v) ((struct eswin_vif *) (v)->drv_priv)

static inline int hw_vifindex(struct ieee80211_vif *vif)
{
	struct eswin_vif *i_vif;

	if (vif == NULL)
		return 0;

	i_vif = to_i_vif(vif);
	return i_vif->index;
}

/* sta driver data structure */
struct eswin_sta {
	struct eswin *tr;
	struct ieee80211_vif *vif;
	/*struct ieee80211_sta *sta;*/

	enum ieee80211_sta_state state;
	struct list_head list;

	/* keys */
	struct ieee80211_key_conf *ptk;
	struct ieee80211_key_conf *gtk;

	/* BSS max idle period */
	struct eswin_capabilities cap;
	struct eswin_max_idle max_idle;
};

#define to_ieee80211_sta(s) \
	container_of((void *)s, struct ieee80211_sta, drv_priv)

#define to_i_sta(s) ((struct eswin_sta *) (s)->drv_priv)



struct eswin_sta_handler {
	int (*sta_state)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 enum ieee80211_sta_state old_state,
			 enum ieee80211_sta_state new_state);

};

#if 0
#define STAH(fn)					\
	static struct eswin_sta_handler __stah_ ## fn	\
	__attribute((__used__))				\
	__attribute((__section__("nrc.sta"))) = {	\
		.sta_state = fn,			\
	}

extern struct eswin_sta_handler __sta_h_start, __sta_h_end;
#endif


/* trx */

struct eswin_trx_data {
	struct eswin *tr;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct sk_buff *skb;
	int result;
};


#define NL80211_IFTYPE_ALL (BIT(NUM_NL80211_IFTYPES)-1)
int eswin_core_register(struct eswin *tr);
void eswin_core_unregister(struct eswin *tr);
struct eswin * eswin_core_create(size_t priv_size, struct device *dev, struct usb_ops * ops);
void eswin_core_destroy(struct eswin *tr);

extern int power_save;
extern int disable_cqm;
#ifdef CONFIG_ECRNX_WIFO_CAIL
extern bool amt_mode;
#endif
extern bool set_gain;
extern bool dl_fw;
extern bool register_status;

#endif
