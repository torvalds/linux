#ifndef __ROCKCHIP_HDMI_CEC_H__
#define __ROCKCHIP_HDMI_CEC_H__
#include "rockchip-hdmi.h"

#include <linux/input.h>

enum {
	CEC_LOGADDR_TV          = 0x00,
	CEC_LOGADDR_RECDEV1     = 0x01,
	CEC_LOGADDR_RECDEV2     = 0x02,
	CEC_LOGADDR_TUNER1      = 0x03,     /* STB1 in Spev v1.3 */
	CEC_LOGADDR_PLAYBACK1   = 0x04,     /* DVD1 in Spev v1.3 */
	CEC_LOGADDR_AUDSYS      = 0x05,
	CEC_LOGADDR_TUNER2      = 0x06,     /* STB2 in Spec v1.3 */
	CEC_LOGADDR_TUNER3      = 0x07,     /* STB3 in Spec v1.3 */
	CEC_LOGADDR_PLAYBACK2   = 0x08,     /* DVD2 in Spec v1.3 */
	CEC_LOGADDR_RECDEV3     = 0x09,
	CEC_LOGADDR_TUNER4      = 0x0A,     /* RES1 in Spec v1.3 */
	CEC_LOGADDR_PLAYBACK3   = 0x0B,     /* RES2 in Spec v1.3 */
	CEC_LOGADDR_RES3        = 0x0C,
	CEC_LOGADDR_RES4        = 0x0D,
	CEC_LOGADDR_FREEUSE     = 0x0E,
	CEC_LOGADDR_UNREGORBC   = 0x0F

};

enum {                   /* CEC Messages */
	CECOP_FEATURE_ABORT			= 0x00,
	CECOP_IMAGE_VIEW_ON			= 0x04,
	CECOP_TUNER_STEP_INCREMENT		= 0x05,
	CECOP_TUNER_STEP_DECREMENT		= 0x06,
	CECOP_TUNER_DEVICE_STATUS		= 0x07,
	CECOP_GIVE_TUNER_DEVICE_STATUS		= 0x08,
	CECOP_RECORD_ON				= 0x09,
	CECOP_RECORD_STATUS			= 0x0A,
	CECOP_RECORD_OFF			= 0x0B,
	CECOP_TEXT_VIEW_ON			= 0x0D,
	CECOP_RECORD_TV_SCREEN			= 0x0F,
	CECOP_GIVE_DECK_STATUS			= 0x1A,
	CECOP_DECK_STATUS			= 0x1B,
	CECOP_SET_MENU_LANGUAGE			= 0x32,
	CECOP_CLEAR_ANALOGUE_TIMER		= 0x33,     /* Spec 1.3A */
	CECOP_SET_ANALOGUE_TIMER		= 0x34,     /* Spec 1.3A */
	CECOP_TIMER_STATUS			= 0x35,     /* Spec 1.3A */
	CECOP_STANDBY				= 0x36,
	CECOP_PLAY				= 0x41,
	CECOP_DECK_CONTROL			= 0x42,
	CECOP_TIMER_CLEARED_STATUS		= 0x43,     /* Spec 1.3A */
	CECOP_USER_CONTROL_PRESSED		= 0x44,
	CECOP_USER_CONTROL_RELEASED		= 0x45,
	CECOP_GIVE_OSD_NAME			= 0x46,
	CECOP_SET_OSD_NAME			= 0x47,
	CECOP_SET_OSD_STRING			= 0x64,
	CECOP_SET_TIMER_PROGRAM_TITLE		= 0x67,	/* Spec 1.3A */
	CECOP_SYSTEM_AUDIO_MODE_REQUEST		= 0x70,	/* Spec 1.3A */
	CECOP_GIVE_AUDIO_STATUS			= 0x71,	/* Spec 1.3A */
	CECOP_SET_SYSTEM_AUDIO_MODE		= 0x72,	/* Spec 1.3A */
	CECOP_REPORT_AUDIO_STATUS		= 0x7A,	/* Spec 1.3A */
	CECOP_GIVE_SYSTEM_AUDIO_MODE_STATUS	= 0x7D,	/* Spec 1.3A */
	CECOP_SYSTEM_AUDIO_MODE_STATUS		= 0x7E,	/* Spec 1.3A */
	CECOP_ROUTING_CHANGE			= 0x80,
	CECOP_ROUTING_INFORMATION		= 0x81,
	CECOP_ACTIVE_SOURCE			= 0x82,
	CECOP_GIVE_PHYSICAL_ADDRESS		= 0x83,
	CECOP_REPORT_PHYSICAL_ADDRESS		= 0x84,
	CECOP_REQUEST_ACTIVE_SOURCE		= 0x85,
	CECOP_SET_STREAM_PATH			= 0x86,
	CECOP_DEVICE_VENDOR_ID			= 0x87,
	CECOP_VENDOR_COMMAND			= 0x89,
	CECOP_VENDOR_REMOTE_BUTTON_DOWN		= 0x8A,
	CECOP_VENDOR_REMOTE_BUTTON_UP		= 0x8B,
	CECOP_GIVE_DEVICE_VENDOR_ID		= 0x8C,
	CECOP_MENU_REQUEST			= 0x8D,
	CECOP_MENU_STATUS			= 0x8E,
	CECOP_GIVE_DEVICE_POWER_STATUS		= 0x8F,
	CECOP_REPORT_POWER_STATUS		= 0x90,
	CECOP_GET_MENU_LANGUAGE			= 0x91,
	CECOP_SELECT_ANALOGUE_SERVICE		= 0x92,     /* Spec 1.3A */
	CECOP_SELECT_DIGITAL_SERVICE		= 0x93,
	CECOP_SET_DIGITAL_TIMER			= 0x97,     /* Spec 1.3A */
	CECOP_CLEAR_DIGITAL_TIMER		= 0x99,     /* Spec 1.3A */
	CECOP_SET_AUDIO_RATE			= 0x9A,     /* Spec 1.3A */
	CECOP_INACTIVE_SOURCE			= 0x9D,     /* Spec 1.3A */
	CECOP_CEC_VERSION			= 0x9E,     /* Spec 1.3A */
	CECOP_GET_CEC_VERSION			= 0x9F,     /* Spec 1.3A */
	CECOP_VENDOR_COMMAND_WITH_ID		= 0xA0,     /* Spec 1.3A */
	CECOP_CLEAR_EXTERNAL_TIMER		= 0xA1,     /* Spec 1.3A */
	CECOP_SET_EXTERNAL_TIMER		= 0xA2,     /* Spec 1.3A */
	CDCOP_HEADER				= 0xF8,
	CECOP_ABORT				= 0xFF,

	CECOP_REPORT_SHORT_AUDIO		= 0xA3,     /* Spec 1.4 */
	CECOP_REQUEST_SHORT_AUDIO		= 0xA4,     /* Spec 1.4 */

	CECOP_ARC_INITIATE			= 0xC0,
	CECOP_ARC_REPORT_INITIATED		= 0xC1,
	CECOP_ARC_REPORT_TERMINATED		= 0xC2,

	CECOP_ARC_REQUEST_INITIATION		= 0xC3,
	CECOP_ARC_REQUEST_TERMINATION		= 0xC4,
	CECOP_ARC_TERMINATE			= 0xC5,

};

/* Operands for <Feature Abort> Opcode */
enum {
	CECAR_UNRECOG_OPCODE            = 0x00,
	CECAR_NOT_CORRECT_MODE,
	CECAR_CANT_PROVIDE_SOURCE,
	CECAR_INVALID_OPERAND,
	CECAR_REFUSED
};

/* Operands for <Power Status> Opcode */
enum {
	CEC_POWERSTATUS_ON              = 0x00,
	CEC_POWERSTATUS_STANDBY         = 0x01,
	CEC_POWERSTATUS_STANDBY_TO_ON   = 0x02,
	CEC_POWERSTATUS_ON_TO_STANDBY   = 0x03,
};

enum {
	EVENT_RX_FRAME,
	EVENT_ENUMERATE,
};

#define MAKE_SRCDEST(src, dest)    ((src << 4) | dest)

#define MAX_CMD_SIZE 16

struct cec_framedata {
	char srcdestaddr; /* Source in upper nybble, dest in lower nybble */
	char opcode;
	char args[MAX_CMD_SIZE];
	char argcount;
	char nextframeargcount;
};

struct cec_delayed_work {
	struct delayed_work work;
	int event;
	void *data;
};

struct cec_device {
	struct workqueue_struct *workqueue;
	struct hdmi *hdmi;
	int address_phy;
	int address_logic;
	int powerstatus;
	char cecval[32];

	int (*sendframe)(struct hdmi *, struct cec_framedata *);
	int (*readframe)(struct hdmi *, struct cec_framedata *);
	void (*setceclogicaddr)(struct hdmi *, int);
};

#ifdef DEBUG
#define CECDBG(format, ...) \
		pr_info(format, ## __VA_ARGS__)
#else
#define CECDBG(format, ...)
#endif

/*====================================
//used for cec key control direction  OK and back
====================================*/
enum  {
	S_CEC_MAKESURE   = 0x0,
	S_CEC_UP         = 0x1,
	S_CEC_DOWN       = 0x2,
	S_CEC_LEFT       = 0x3,
	S_CEC_RIGHT      = 0x4,
	S_CEC_BACK       = 0x0d,
	S_CEC_VENDORBACK = 0x91,
};

int rockchip_hdmi_cec_init(struct hdmi *hdmi,
			   int (*sendframe)(struct hdmi *,
					    struct cec_framedata *),
			   int (*readframe)(struct hdmi *,
					    struct cec_framedata *),
			   void (*setceclogicaddr)(struct hdmi *, int));
void rockchip_hdmi_cec_set_pa(int devpa);
void rockchip_hdmi_cec_submit_work(int event, int delay, void *data);
#endif /* __HDMI_CEC_H__ */