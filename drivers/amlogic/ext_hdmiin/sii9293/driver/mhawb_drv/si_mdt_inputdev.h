








/*
 Silicon Image Driver Extension

 Copyright (C) 2012 Silicon Image Inc.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation version 2.

 This program is distributed .as is. WITHOUT ANY WARRANTY of any
 kind, whether express or implied; without even the implied warranty
 of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the
 GNU General Public License for more details.
*/
//!file     si_mdt_inputdev.h
//!brief    Silicon Image implementation of MDT function.
//
//***************************************************************************/

#ifndef _SI_MDT_INPUTDEV_H_
#define _SI_MDT_INPUTDEV_H_


#include <linux/input.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/hid.h>

#define MDT_SUPPORT
#define WRITE_BURST_ISR
#define ICS_GTi9100

#define MDT_TOUCH_TIMEOUT_ENABLE	1
#define MDT_TOUCH_TIMEOUT_DISABLE	0

#define MDT_MAX_TOUCH_CONTACTS		4
#define MDT_HID_INPUT_REPORT		7
#define KEYBOARD			2
#define MOUSE 				1
#define WAITING_FOR_HEADER		0
#define RECEIVING_PAYLOAD		1
#define RECEIVED			4
#define MEDIA_DATA_TUNNEL_SUPPORT

#define MDT_DPAD_UP			0x01
#define MDT_DPAD_RIGHT			0x02
#define MDT_DPAD_DOWN			0x04
#define MDT_DPAD_LEFT			0x08
	
#define MDT_GAMEXYRZ_X			0
#define MDT_GAMEXYRZ_Y			1
#define MDT_GAMEXYRZ_Z			2
#define MDT_GAMEXYRZ_Rz			3

#define MDT_HID_DPAD_000_DEGREES	0
#define MDT_HID_DPAD_045_DEGREES	1
#define MDT_HID_DPAD_090_DEGREES	2
#define MDT_HID_DPAD_135_DEGREES	3
#define MDT_HID_DPAD_180_DEGREES	4
#define MDT_HID_DPAD_225_DEGREES	5
#define MDT_HID_DPAD_270_DEGREES	6
#define MDT_HID_DPAD_315_DEGREES	7
#define MDT_HID_DPAD_IDLE		8

#define MDT_DPAD_CENTER			0x80
#define MDT_DPAD_ERROR_ALLOWANCE	5			// Ignore DPAD value variance of 5 from center
#define MDT_DPAD_NORMALIZE_RANGE_TO_5	24			// 0x80 / 5 = 25; 24 is close enough

#define MDT_OTHER_BUTTONS_4		0x08
#define MDT_OTHER_BUTTONS_5		0x10

#define TS_TOUCHED			1			//additional macros for PhoenixBlade support
#define KEY_PRESSED			1
#define KEY_RELEASED			0

#define MDT_BUTTON_LEFT			1
#define MDT_BUTTON_RIGHT		2
#define MDT_BUTTON_MIDDLE		4

#define MDT_ERROR			0xFF

#define MDT_TOUCH_X			0
#define MDT_TOUCH_Y			1

#define BYTE_LOW			0
#define BYTE_HIGH			1

#define MDT_TOUCH_X_LOW			BYTE_LOW
#define MDT_TOUCH_X_HIGH		BYTE_HIGH
#define MDT_TOUCH_Y_LOW			BYTE_LOW
#define MDT_TOUCH_Y_HIGH		BYTE_HIGH

//2012-12-12 updating MDT definition & Korvus firmware.
//    Now X and Y relay within 0..1920. Corner definitions must be updated.
#define XY_MAX				1920
#define X_CORNER_RIGHT_LOWER		1860	//relative to physical coordinates with 0,0 in top-left
#define Y_CORNER_RIGHT_LOWER		1860	//relative to physical coordinates with 0,0 in top-left

#if 	defined(ICS_GTi9100)
#define X_MAX				800	//	touch.deviceType = touchscreen
#define Y_MAX				760	//	touch.orientationAware = 0
#define SCALE_X_RAW			XY_MAX	//	devices.internal = 0
#define SCALE_X_SCREEN			500	// These values were experimentally selected based on	
#define SCALE_Y_RAW			XY_MAX	//	MDT sending 1920 x 1920
#define SCALE_Y_SCREEN			500	// 	scaling SCREEN/RAW min=0; max=X_MAX
#define SINGLE_TOUCH			0
#define X_SHIFT				0
#define Y_SHIFT				0
#define SWAP_LEFTRIGHT			0
#define SWAP_UPDOWN			0
#define SWAP_XY				0
#define CORNER_BUTTON			0
#define ICS_BAR				0
#define KERNEL_2_6_38_AND_LATER

#endif
#ifdef __KERNEL__
	typedef struct input_dev	 	si_input_dev;
	
	#define si_input_report_key(x,y,z)	input_report_key(x, y, z)
	#define si_input_report_rel(x,y,z)	input_report_rel(x, y, z)		
	#define si_input_sync(x)     		input_sync(x);
	#define si_input_allocate_device()	input_allocate_device()
	#define si_set_bit(x,y)			set_bit(x,y)

	#define SI_MDT_DEBUG_PRINT(string,args...)		printk(KERN_ERR string,##args)
#else
	typedef unsigned char si_input_dev;

	#include "..\..\platform\api\si_osdebug.h"
	
	#define SI_MDT_DEBUG_PRINT(x)		CBUS_DEBUG_PRINT(x)

	#define si_input_report_key(x,y,z)	SI_MDT_DEBUG_PRINT(("MDTkeyboard  event %02x %02x\n",(int)y,(int)z))
	#define si_input_report_rel(x,y,z)	SI_MDT_DEBUG_PRINT(("MDTnon-mouse event %02x %02x\n",(int)y,(int)z))
	#define si_input_sync(x)		SI_MDT_DEBUG_PRINT(("Submit HID event\n"))
	#define si_input_allocate_device()	0
	#define si_set_bit(x,y)			SI_MDT_DEBUG_PRINT(("MDTkeyboard config %02x\n", (int)y))

	unsigned char *memscan(unsigned char *str_a, unsigned char key, unsigned char length);

	#define ENOMEM 0x1
	#define EV_REP 0x2
#endif

#define mdt_event_header		\
	unsigned char				isNotLast:1;	\
	unsigned char				isKeyboard:1;	\
	unsigned char				isPortB:1; 	\
	unsigned char          			isHID  :1;		

#define mdt_rawevent_header		\
	unsigned char				isRW:1;		\
	unsigned char				isRequest:1;	\
	unsigned char				isPriority:1; 	\
	unsigned char          		isHID:1;		


#define mdt_cursor_suffix		\
	unsigned char				reserved:1;	\
	unsigned char				isGame:1;


struct mdt_mouse_XYZ_t{
    signed char      		        x_byteLen;
    signed char      		        y_byteLen;
    signed char      		        z_byteLen;
    unsigned char        		      	reserved[3];
};

struct mdt_mouse_XYZ_VS_t{
	unsigned char          	    	xyz_byteLen[3];
	struct {
		unsigned char          	byte_8bit[2];
		unsigned char        	   	byte_6bit:6;
		mdt_cursor_suffix	// common, 2 bit suffix
	} vendor_specific;
};

struct mdt_touch_XYZ_t{
    unsigned char				xy_wordLen[2][2];
	struct {
		unsigned char			byte_8bit;
		unsigned char			byte_6bit:6;
		mdt_cursor_suffix	// common, 2 bit suffix
	} vendor_specific;    
};

struct mdt_game_XYZRz_t{
	unsigned char      		        xyzRz_byteLen[4];
	unsigned char      		        buttons_ex;
	unsigned char      		        dPad:4;
	unsigned char      		        deviceID:2;
	mdt_cursor_suffix	   	// common, 2 bit suffix
};

struct mdt_suffix{
	unsigned char				other_data[5];
	unsigned char	        	   	other_data_bits:6;
	mdt_cursor_suffix	   	// common, 2 bit suffix
};


struct mdt_non_mouse_cursorheader_t{
	unsigned char          		isTouched:1;
	unsigned char				contactID:2;
	unsigned char				isNotMouse:1;
	mdt_event_header		// common, 4 bit header nibble
};

struct mdt_mouse_cursorheader_t {    
	unsigned char          		button:3;
	unsigned char				isNotMouse:1;
	mdt_event_header		// common, 4 bit header	nibble
} ;


struct mdt_cursor_mouse_t{	  	// 4 bytes or 7 bytes in length
  	struct mdt_mouse_cursorheader_t	header;		//use when (!IsNotMouse)
	union {
	 	struct mdt_mouse_XYZ_t	  XYZ;		//use when (!IsNotLast) 
		struct mdt_mouse_XYZ_VS_t XYZ_VS;	//use when (!IsNotMouse) && (IsNotLast)
		unsigned char		raw[6];
	} body;
};

struct mdt_cursor_other_t{		// 4 bytes or 7 bytes in length			 
	union {
	  	struct mdt_non_mouse_cursorheader_t 	touch;  //use when (IsNotMouse)
		struct mdt_mouse_cursorheader_t		game;
		unsigned char				raw;
	} header;
	union {
		struct mdt_touch_XYZ_t			touchXYZ;	//use when (IsNotMouse) && (!IsGame)
		struct mdt_game_XYZRz_t			gameXYZRz;	//use wehn (IsNotMouse) && (IsGame)
		struct mdt_suffix			suffix;
		unsigned char 				raw[6];
	} body;
};

struct mdt_keyboard_header_t{
  	unsigned char	   			modifier_keys :3;
	unsigned char	   			reserved:1;			//set to 0
	mdt_event_header
};

struct mdt_raw_with_header {
  	unsigned char	   			ls_nibble :4;
	mdt_rawevent_header
};

struct mdt_keyboard_event_t{          	//4 bytes or 7 bytes in length
	struct mdt_keyboard_header_t	header;	// to avoid wasting space, all bit fields must be in the same struct
	union {
		struct {
       			unsigned char		keycodes_firstThree[3];
       			unsigned char		reserved [3];
     		} truncated;
		unsigned char			keycodes_all[6];
	} body;
};

struct mdt_header_first {	       	//2012-05-10 - add RAW packet structure
	unsigned char	   			ls_nibble :4;
	mdt_event_header
	unsigned char				other_data_suffix[7];
};

union mdt_event_t{			// 4 bytes 7 bytes in length
	struct mdt_cursor_mouse_t	event_mouse;
	struct mdt_cursor_other_t	event_cursor;
	struct mdt_keyboard_event_t  	event_keyboard;
	struct mdt_header_first		header;
	unsigned char			bytes[7];
};

struct mdt_burst_01_t{	     		// 6, 9, 13, or 16 bytes
	unsigned char          		ADOPTER_ID[2];
	union mdt_event_t		events[2];
};


// -------------------------------------------------------------------------------
// INPUT device instance definition

#define INPUT_DISABLED				0
#define INPUT_WAITING_FOR_REGISTRATION		1
#define INPUT_ACTIVE				2

#define DEV_TYPE_MOUSE 				0
#define DEV_TYPE_KEYBOARD			1
#define DEV_TYPE_TOUCH				2
#define DEV_TYPE_GAME				3
#define DEV_TYPE_COUNT				4


#define MDT_TOUCH_INACTIVE			1
#define MDT_TOUCH_ACTIVE			2

#define MDT_VERSION				1
#define M_CHAR					'M'
#define D_CHAR					'D'
#define T_CHAR					'T'

#define NOTICE_DEV_PLUG				'R'
#define NOTICE_DEV_UNPLUG			'U'
#define RESPONSE_ACK				'A'	
#define RESPONSE_NACK				'N'

#define PARSE_FAILURE				4
#define PARSE_HP_ERROR				2
#define PARSE_HID_ERROR				1
#define PARSE_HID_OK				0

#define MDT_VERSION_1				1

#define REGISTRATION_SUCCESS			0
#define REGISTRATION_ERROR			1

struct gamepad_history_t {
	unsigned int			abs_x;
	unsigned int			abs_y;
	int				x_delta;
	int				y_delta;
	unsigned char 			dpad_event;
	unsigned char			other_buttons;	
};

struct mdt_touch_history_t{
	unsigned int			abs_x;		//Cached coordinate values for multi-touch HIDs
	unsigned int			abs_y;		//	the array is limited to 3 members since MDT packet
	unsigned char			isTouched;	//	structure only has 2 bits to ID the contact
	unsigned char			wasIsTouched;
	unsigned char			state;
	unsigned int			time_of_last_contact;	//2013-04-25... offer release timeout
};

struct si_mdt_inputdevs_t {
	struct device	   	 	*g_mdt_dev;			// Debug variables
	struct class 	    		*g_mdt_class;

	struct workqueue_struct 	*mdt_joystick_wq;
	struct delayed_work 		repeat_for_gamepad;

	struct workqueue_struct 	*mdt_touchscreen_wq;
	struct delayed_work 		timeout_for_touchscreen;
	unsigned char			touchscreen_timer_on;

	unsigned char			prior_mouse_buttons;
	struct gamepad_history_t	prior_game_event;	
	struct mdt_touch_history_t	prior_touch_events[MDT_MAX_TOUCH_CONTACTS];	
	unsigned char			prior_touch_button;
#ifdef  PHOENIX_BLADE
	unsigned char 			touch_debounce_counter;
	enum phoenix_blade_demo_state_e	phoenix_blade_state;
	struct mdt_touch_history_t	prior_native_touch;
	struct mdt_double_touch_t	double_touch;	
	struct mdt_simulated_buttons_t	simulated;
#endif
	unsigned char 			is_dev_registered[DEV_TYPE_COUNT];// Instance tracking variables
	unsigned char			keycodes_old[MDT_HID_INPUT_REPORT];	// Prior HID input report
	unsigned char			keycodes_new[MDT_HID_INPUT_REPORT]; // Current HID input report
	unsigned char			*key_map;	

	//user overrides
	unsigned int			x_max, x_screen, x_raw, x_shift;
	unsigned int			y_max, y_screen, y_raw, y_shift;
	unsigned int			swap_xy, swap_updown, swap_leftright;

#ifdef MHL_BT_HID_DEMO
	unsigned char			is_ctrl_modifier;
#endif

	//note: Unsuccesfully tried to use an array of pointers for si_input_dev. This didn't work.
	si_input_dev			*dev_touchscreen;
	si_input_dev			*dev_keyboard;			// Input devices are event generating interfaces in 
	si_input_dev 			*dev_mouse;			//    the Linux input subsystem. Such devices
									//    are typically located under /dev/input/<xyz>
} ;									//    Linux file system. These devices can be read but,
									//    cannot be written. When read, the data retrieved

#define MDT_DISCOVERY_SIZE		4
#define MDT_DISCOVERY_DISABLE		1

#define MDT_MIN_PACKET_LENGTH		4
#define MDT_KEYBOARD_PACKET_TAIL_LENGTH	3
#define MDT_KEYBOARD_PACKET_LENGTH	(MDT_MIN_PACKET_LENGTH + MDT_KEYBOARD_PACKET_TAIL_LENGTH)
#define MDT_TOUCH_PACKET_LENGTH		5
#define MDT_MOUSE_PACKET_LENGTH		MDT_MIN_PACKET_LENGTH
#define MDT_MAX_PACKET_LENGTH		MDT_KEYBOARD_PACKET_LENGTH

        
#define MDT_SYNC_ERROR_OFFSET		8
#define MDT_SYNC_ERROR_FLAG_MASK	0xF0
#define MDT_SYNC_ERROR_VALUE_MASK	0x0F
#define MDT_SEND_ERROR_FLAG		0x80
#define MDT_SYNC_ERROR_FLAG		0


#define MDT_EVENT_HANDLED		1

									//    will reflect event time, source, & value.
unsigned char  	mdt_input_init(void);
void	 	mdt_input_deregister(void);
uint8_t		mdt_deregister_device(unsigned char mdt_device_type, unsigned char isReset);
uint8_t		mdt_register_device(unsigned char mdt_device_type);
void		mdt_generate_event_keyboard(struct mdt_keyboard_event_t *keyboardPacket);
void		mdt_generate_event_mouse(struct mdt_cursor_mouse_t *mousePacket);
void 	 	mdt_generate_event_gamepad(struct mdt_cursor_other_t *gamePacket);
unsigned char	mdt_generate_event_touchscreen(struct mdt_cursor_other_t *touchPacket, 
			unsigned char submitEvent, unsigned char isOnTouchTimeout);
unsigned char	mdt_generate_event_mouse_from_nativetouch(struct mdt_touch_history_t *touch_event);
void		mdt_reset_touch(void);

#ifdef MHL_BT_HID_DEMO
unsigned char   mhl_bt_hid_demo_key_to_touch(unsigned char scancode, unsigned char value);
#endif

enum 	mhawb_sendapireturn_e {
	MHAWB_SENDAPI_SUCCESS			= 0
	, MHAWB_SENDAPI_ERROR_FIFOFULL		= 0x80
	, MHAWB_SENDAPI_ERROR_INVALIDTOUCHDATA	= 0x81
	, MHAWB_SENDAPI_ERROR_WRITEBURSTFAILURE	= 0x82
	, MHAWB_SENDAPI_ERROR_UNKNOWNMHLERROR	= 0x83
};

enum 	mhawb_updateapireturn_e {
	MHAWB_UPDATEAPI_SUCCESS			= 0
	, MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE	= 0x84
	, MHAWB_UPDATEAPI_ERROR_UNSUPPORTEDFIELD= 0x85
};

enum 	mhawb_syncapireturn_e {
	MHAWB_SYNCAPI_SUCCESS			= 0x00
	, MHAWB_SYNCAPI_SUCCESS_MDTMOUSE_SENT	= 0x10
	, MHAWB_SYNCAPI_SUCCESS_MDTKEYBOARD_SENT= 0x20
	, MHAWB_SYNCAPI_SUCCESS_MDTTOUCH_SENT	= 0x40
	, MHAWB_SYNCAPI_SUCCESS_MDTGAME_SENT	= 0x80
	, MHAWB_SYNCAPI_ERROR_PRIORPENDING	= 0x08
	, MHAWB_SYNCAPI_ERROR_INVALIDDATA	= 0x09
	, MHAWB_SYNCAPI_ERROR_WRITEBURSTFAILURE	= 0x0A
	, MHAWB_SYNCAPI_ERROR_UNKNOWNERROR	= 0x0B
};


uint8_t mdt_burst_send_touch(struct mdt_cursor_other_t *touchPacket);
//uint8_t mdt_burst_value_update(struct hid_usage *usage, __s32 value);
//uint8_t mdt_burst_sync_and_send(void);
#endif
