/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ROCKCHIP_HDMIV1_HDCP_H__
#define __ROCKCHIP_HDMIV1_HDCP_H__

/***************************/
/* Definitions             */
/***************************/

/* Status / error codes */
#define HDCP_OK			0
#define HDCP_KEY_ERR		1
#define HDCP_KSV_ERR		2
#define HDCP_KEY_VALID		3
#define HDCP_KEY_INVALID	4

/* Delays */
#define HDCP_ENABLE_DELAY	300
#define HDCP_REAUTH_DELAY	100

/* Event source */
#define HDCP_SRC_SHIFT		8
#define HDCP_IOCTL_SRC		(0x1 << HDCP_SRC_SHIFT)
#define HDCP_HDMI_SRC		(0x2 << HDCP_SRC_SHIFT)
#define HDCP_IRQ_SRC		(0x4 << HDCP_SRC_SHIFT)
#define HDCP_WORKQUEUE_SRC	(0x8 << HDCP_SRC_SHIFT)

/* Event */
#define HDCP_ENABLE_CTL		(HDCP_IOCTL_SRC	| 0)
#define HDCP_DISABLE_CTL	(HDCP_IOCTL_SRC	| 1)
#define HDCP_START_FRAME_EVENT	(HDCP_HDMI_SRC	| 2)
#define HDCP_STOP_FRAME_EVENT	(HDCP_HDMI_SRC	| 3)
#define HDCP_KSV_LIST_RDY_EVENT	(HDCP_IRQ_SRC	| 4)
#define HDCP_FAIL_EVENT		(HDCP_IRQ_SRC	| 5)
#define HDCP_AUTH_PASS_EVENT	(HDCP_IRQ_SRC	| 6)
#define HDCP_AUTH_REATT_EVENT	(HDCP_WORKQUEUE_SRC | 7)

/* Key size */
#define HDCP_KEY_SIZE			308

/* HDCP DDC Clock */
#define HDCP_DDC_CLK			100000

/* Authentication retry times */
#define HDCP_INFINITE_REAUTH	0x100

/* HDCP Regs */
#define HDCP_CTRL1				0x52
	#define m_AUTH_START		BIT(7)
	#define m_BKSV_VALID		BIT(6)
	#define m_BKSV_INVALID		BIT(5)
	#define m_ENCRYPT_ENABLE	BIT(4)
	#define m_AUTH_STOP		BIT(3)
	#define m_ADVANED_ENABLE	BIT(2)
	#define m_HDMI_DVI		BIT(1)
	#define m_HDCP_RESET		BIT(0)

	#define v_AUTH_START(n)		(n << 7)
	#define v_BKSV_VALID(n)		(n << 6)
	#define v_BKSV_INVALID(n)	(n << 5)
	#define v_ENCRYPT_ENABLE(n)	(n << 4)
	#define v_AUTH_STOP(n)		(n << 3)
	#define v_ADVANED_ENABLE(n)	(n << 2)
	#define v_HDMI_DVI(n)		(n << 1)
	#define v_HDCP_RESET(n)		(n << 0)

#define HDCP_CTRL2				0x53
	#define m_DISABLE_127_CHECK			BIT(7)
	#define m_SKIP_BKSV_CHECK			BIT(6)
	#define m_ENABLE_PJ_CHECK			BIT(5)
	#define m_DISABLE_DEVICE_NUMBER_CHECK		BIT(4)
	#define m_DELAY_RI_1_CLK			BIT(3)
	#define m_USE_PRESET_AN				BIT(2)
	#define m_KEY_COMBINATION			(BIT(1) | BIT(0))

	#define v_DISABLE_127_CHECK(n)			(n << 7)
	#define v_SKIP_BKSV_CHECK(n)			(n << 6)
	#define v_ENABLE_PJ_CHECK(n)			(n << 5)
	#define v_DISABLE_DEVICE_NUMBER_CHECK(n)(n << 4)
	#define v_DELAY_RI_1_CLK(n)				(n << 3)
	#define v_USE_PRESET_AN(n)				(n << 2)
	#define v_KEY_COMBINATION(n)			(n << 0)

#define HDCP_KEY_STATUS			0x54
	#define m_KEY_READY			BIT(0)

#define HDCP_CTRL_SOFT			0x57
	#define m_DISABLE_127_CHECK			BIT(7)
	#define m_SKIP_BKSV_CHECK			BIT(6)
	#define m_NOT_AUTHENTICATED			BIT(5)
	#define m_ENCRYPTED				BIT(4)
	#define m_ADVANCED_CIPHER			BIT(3)

#define HDCP_BCAPS_RX			0x58
#define HDCP_TIMER_100MS		0x63
#define HDCP_TIMER_5S			0x64
#define HDCP_ERROR				0x65
	#define m_DDC_NO_ACK		BIT(3)
	#define m_PJ_MISMACH		BIT(2)
	#define m_RI_MISMACH		BIT(1)
	#define m_BKSV_WRONG		BIT(0)

#define HDCP_KSV_BYTE0			0x66
#define HDCP_KSV_BYTE1			0x67
#define HDCP_KSV_BYTE2			0x68
#define HDCP_KSV_BYTE3			0x69
#define HDCP_KSV_BYTE4			0x6a

#define HDCP_AN_SEED			0x6c

#define HDCP_BCAPS_TX			0x80
#define HDCP_BSTATE_0			0x81
#define HDCP_BSTATE_1			0x82

#define HDCP_KEY_FIFO			0x98

#define HDCP_INT_MASK1			0xc2
#define HDCP_INT_STATUS1		0xc3
	#define m_INT_HDCP_ERR		BIT(7)
	#define m_INT_BKSV_READY	BIT(6)
	#define m_INT_BKSV_UPDATE	BIT(5)
	#define m_INT_AUTH_SUCCESS	BIT(4)
	#define m_INT_AUTH_READY	BIT(3)

#define HDCP_INT_MASK2			0xc4
#define HDCP_INT_STATUS2		0xc5
	#define m_INT_SOFT_MODE_READY			BIT(7)
	#define m_INT_AUTH_M0_REDAY			BIT(6)
	#define m_INT_1st_FRAME_ARRIVE			BIT(5)
	#define m_INT_AN_READY				BIT(4)
	#define m_INT_ENCRYPTED				BIT(2)
	#define m_INT_NOT_ENCRYPTED_AVMUTE		BIT(1)
	#define m_INT_NOT_ENCRYPTED_AVUNMUTE	BIT(0)

enum hdcp_states {
	HDCP_DISABLED,
	HDCP_ENABLE_PENDING,
	HDCP_AUTHENTICATION_START,
	HDCP_WAIT_KSV_LIST,
	HDCP_LINK_INTEGRITY_CHECK,
};

enum hdmi_states {
	HDMI_STOPPED,
	HDMI_STARTED
};

#define HDCP_PRIVATE_KEY_SIZE	280
#define HDCP_KEY_SHA_SIZE		20

struct hdcp_keys {
	u8 ksv[8];
	u8 devicekey[HDCP_PRIVATE_KEY_SIZE];
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
	struct mutex lock;	/* use for workqueue */
	struct completion	complete;
	struct workqueue_struct *workqueue;

	enum hdmi_states hdmi_state;
	enum hdcp_states hdcp_state;

	struct delayed_work *pending_start;
	struct delayed_work *pending_wq_event;
	int retry_cnt;
	int auth_state;
	struct hdmi_dev *hdmi_dev;
};

#if 1
#define HDCP_WARN(x...) pr_warn(x)
#else
#define I2S_DBG(x...) do { } while (0)
#endif
int	rockchip_hdmiv1_hdcp_init(struct hdmi *hdmi);
#endif /* __ROCKCHIP_HDMIV1_HDCP_H__ */
