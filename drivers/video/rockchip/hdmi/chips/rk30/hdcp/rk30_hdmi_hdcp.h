#ifndef __RK30_HDMI_HDCP_H__
#define __RK30_HDMI_HDCP_H__

/***************************/
/* Definitions             */
/***************************/

/* Status / error codes */
enum {
	HDCP_OK,
	HDCP_KEY_ERR,
	HDCP_DDC_ERROR,
	HDCP_AUTH_FAILURE,
	HDCP_AKSV_ERROR,
	HDCP_BKSV_ERROR,
	HDCP_CANCELLED_AUTH,
	HDCP_BKSVLIST_TIMEOUT,
};

/* Delays */
#define HDCP_ENABLE_DELAY	300
#define HDCP_REAUTH_DELAY	100
#define HDCP_R0_DELAY		120
#define HDCP_KSV_TIMEOUT_DELAY  5000
/***********************/
/* HDCP DDC addresses  */
/***********************/

#define DDC_BKSV_ADDR		0x00
#define DDC_Ri_ADDR		0x08
#define DDC_AKSV_ADDR		0x10
#define DDC_AN_ADDR		0x18
#define DDC_V_ADDR		0x20
#define DDC_BCAPS_ADDR		0x40
#define DDC_BSTATUS_ADDR	0x41
#define DDC_KSV_FIFO_ADDR	0x43

#define DDC_BKSV_LEN		5
#define DDC_Ri_LEN		2
#define DDC_AKSV_LEN		5
#define DDC_AN_LEN		8
#define DDC_V_LEN		4//20
#define DDC_BCAPS_LEN		1
#define DDC_BSTATUS_LEN		2

/* Event source */
#define HDCP_SRC_SHIFT		8
#define HDCP_IOCTL_SRC		(0x1 << HDCP_SRC_SHIFT)
#define HDCP_HDMI_SRC		(0x2 << HDCP_SRC_SHIFT)
#define HDCP_IRQ_SRC		(0x4 << HDCP_SRC_SHIFT)
#define HDCP_WORKQUEUE_SRC	(0x8 << HDCP_SRC_SHIFT)

/* Event */
#define HDCP_ENABLE_CTL			(HDCP_IOCTL_SRC		| 0)
#define HDCP_DISABLE_CTL		(HDCP_IOCTL_SRC		| 1)
#define HDCP_START_FRAME_EVENT	(HDCP_HDMI_SRC		| 2)
#define HDCP_STOP_FRAME_EVENT	(HDCP_HDMI_SRC		| 3)
#define HDCP_FAIL_EVENT			(HDCP_IRQ_SRC		| 4)
#define HDCP_AUTH_PASS_EVENT	(HDCP_IRQ_SRC		| 5)
#define HDCP_AUTH_START_1ST		(HDCP_IRQ_SRC		| 6)
#define HDCP_RI_EXP_EVENT		(HDCP_IRQ_SRC		| 7)
#define HDCP_AUTH_REATT_EVENT	(HDCP_WORKQUEUE_SRC	| 8)
#define HDCP_R0_EXP_EVENT		(HDCP_WORKQUEUE_SRC	| 9)
#define HDCP_AUTH_START_2ND		(HDCP_WORKQUEUE_SRC	| 10)

/* Key size */
#define HDCP_KEY_SIZE			308	

/* Authentication retry times */
#define HDCP_INFINITE_REAUTH	0x100

/* Max downstream device number */
#define MAX_DOWNSTREAM_DEVICE_NUM	1

enum hdcp_states {
	HDCP_DISABLED,
	HDCP_ENABLE_PENDING,
	HDCP_AUTHENTICATION_START,
	HDCP_AUTHENTICATION_1ST,
	HDCP_WAIT_R0_DELAY,
	HDCP_WAIT_KSV_LIST,
	HDCP_LINK_INTEGRITY_CHECK,
};

enum hdmi_states {
	HDMI_STOPPED,
	HDMI_STARTED
};

#define HDCP_PRIVATE_KEY_SIZE	280
#define HDCP_KEY_SHA_SIZE		20
#define HDCP_DDC_CLK			100000

struct hdcp_keys{
	u8 KSV[8];
	u8 DeviceKey[HDCP_PRIVATE_KEY_SIZE];
	u8 sha1[HDCP_KEY_SHA_SIZE];
};

struct hdcp_delayed_work {
	struct delayed_work work;
	int event;
};

struct hdcp {
	int	enable;
	int retry_times;
	struct hdcp_keys *keys;
	int invalidkey;
	char *invalidkeys;	
	struct mutex lock;
	struct completion	complete;
	struct workqueue_struct *workqueue;
	
	enum hdmi_states hdmi_state;
	enum hdcp_states hdcp_state;
	
	struct delayed_work *pending_start;
	struct delayed_work *pending_wq_event;
	int retry_cnt;
	int pending_disable;
	struct hdmi* hdmi;
};

#define SOFT_HDCP_INT_MASK1	0x96 * 4
	#define m_SF_MODE_READY		(1 << 7)
	
#define SOFT_HDCP_INT_MASK2	0x97 * 4
	#define m_I2C_ACK			(1 << 7)
	#define m_I2C_NO_ACK		(1 << 6)
	
#define SOFT_HDCP_INT1		0x98 * 4
	#define m_SOFT_HDCP_READY			(1 << 7)
	#define m_SOFT_HDCP_RI_READY		(1 << 6)
	#define m_SOFT_HDCP_AN_READY		(1 << 4)
	#define m_SOFT_HDCP_SHA_READY		(1 << 3)
	
#define SOFT_HDCP_INT2		0x99 * 4
	#define m_SOFT_HDCP_RI_SAVED		(1 << 5)
	#define m_SOFT_HDCP_PJ_SAVED		(1 << 4)
	
#define SOFT_HDCP_CTRL1		0x9A * 4
	#define m_SOFT_HDCP_AUTH_EN			(1 << 7)	// enable software hdcp
	#define m_SOFT_HDCP_AUTH_START		(1 << 5)
	#define m_SOFT_HDCP_PREP_AN			(1 << 4)
	#define m_SOFT_HDCP_REPEATER		(1 << 2)
	#define m_SOFT_HDCP_GEN_RI			(1 << 1)
	#define m_SOFT_HDCP_CAL_SHA			(1 << 0)
	#define v_SOFT_HDCP_AUTH_EN(n)		(n << 7)
	#define v_SOFT_HDCP_PREP_AN(n)		(n << 4)
	#define v_SOFT_HDCP_REPEATER(n)		(n << 2)
	#define v_SOFT_HDCP_GEN_RI(n)		(n << 1)
	#define v_SOFT_HDCP_CAL_SHA(n)		(n << 0)
	
#define HDCP_DDC_ACCESS_LENGTH	0x9E * 4
#define	HDCP_DDC_OFFSET_ADDR	0xA0 * 4
#define HDCP_DDC_CTRL			0xA1 * 4
	#define m_DDC_READ			(1 << 0)
	#define m_DDC_WRITE			(1 << 1)

#define SOFT_HDCP_BCAPS			0xE0 * 4

#define HDCP_DDC_READ_BUFF		0xA2 * 4
#define HDCP_DDC_WRITE_BUFF		0xA7 * 4
#define HDCP_AN_BUFF			0xE8 * 4
#define HDCP_AKSV_BUFF			0xBF * 4
#define HDCP_BKSV_BUFF			0xE3 * 4
#define HDCP_RI_BUFF			0xD9 * 4
#define HDCP_BSTATUS_BUFF		0xE1 * 4

#define HDCP_NUM_DEV			0xDC * 4
#define HDCP_SHA_BUF			0xB9 * 4
#define HDCP_SHA_INDEX			0xD8 * 4

#ifdef HDCP_DEBUG
#define HDCP_DBG(format, ...) \
		printk(KERN_INFO "HDCP: " format "\n", ## __VA_ARGS__)
#else
#define HDCP_DBG(format, ...)
#endif

extern struct delayed_work *hdcp_submit_work(int event, int delay);
extern void rk30_hdcp_disable(struct hdcp *hdcp);
extern int	rk30_hdcp_start_authentication(struct hdcp *hdcp);
extern int	rk30_hdcp_check_bksv(struct hdcp *hdcp);
extern int	rk30_hdcp_load_key2mem(struct hdcp *hdcp, struct hdcp_keys *key);
extern int	rk30_hdcp_authentication_1st(struct hdcp *hdcp);
extern int	rk30_hdcp_authentication_2nd(struct hdcp *hdcp);
extern void rk30_hdcp_irq(struct hdcp *hdcp);
extern int	rk30_hdcp_lib_step1_r0_check(struct hdcp *hdcp);
extern int	rk30_hdcp_lib_step3_r0_check(struct hdcp *hdcp);
extern u8	hdcp_lib_check_repeater_bit_in_tx(struct hdcp *hdcp);
#endif /* __RK30_HDMI_HDCP_H__ */