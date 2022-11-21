// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Native support for the Aiptek HyperPen USB Tablets
 *  (4000U/5000U/6000U/8000U/12000U)
 *
 *  Copyright (c) 2001      Chris Atenasio   <chris@crud.net>
 *  Copyright (c) 2002-2004 Bryan W. Headley <bwheadley@earthlink.net>
 *
 *  based on wacom.c by
 *     Vojtech Pavlik      <vojtech@suse.cz>
 *     Andreas Bach Aaen   <abach@stofanet.dk>
 *     Clifford Wolf       <clifford@clifford.at>
 *     Sam Mosel           <sam.mosel@computer.org>
 *     James E. Blair      <corvus@gnu.org>
 *     Daniel Egger        <egger@suse.de>
 *
 *  Many thanks to Oliver Kuechemann for his support.
 *
 *  ChangeLog:
 *      v0.1 - Initial release
 *      v0.2 - Hack to get around fake event 28's. (Bryan W. Headley)
 *      v0.3 - Make URB dynamic (Bryan W. Headley, Jun-8-2002)
 *             Released to Linux 2.4.19 and 2.5.x
 *      v0.4 - Rewrote substantial portions of the code to deal with
 *             corrected control sequences, timing, dynamic configuration,
 *             support of 6000U - 12000U, procfs, and macro key support
 *             (Jan-1-2003 - Feb-5-2003, Bryan W. Headley)
 *      v1.0 - Added support for diagnostic messages, count of messages
 *             received from URB - Mar-8-2003, Bryan W. Headley
 *      v1.1 - added support for tablet resolution, changed DV and proximity
 *             some corrections - Jun-22-2003, martin schneebacher
 *           - Added support for the sysfs interface, deprecating the
 *             procfs interface for 2.5.x kernel. Also added support for
 *             Wheel command. Bryan W. Headley July-15-2003.
 *      v1.2 - Reworked jitter timer as a kernel thread.
 *             Bryan W. Headley November-28-2003/Jan-10-2004.
 *      v1.3 - Repaired issue of kernel thread going nuts on single-processor
 *             machines, introduced programmableDelay as a command line
 *             parameter. Feb 7 2004, Bryan W. Headley.
 *      v1.4 - Re-wire jitter so it does not require a thread. Courtesy of
 *             Rene van Paassen. Added reporting of physical pointer device
 *             (e.g., stylus, mouse in reports 2, 3, 4, 5. We don't know
 *             for reports 1, 6.)
 *             what physical device reports for reports 1, 6.) Also enabled
 *             MOUSE and LENS tool button modes. Renamed "rubber" to "eraser".
 *             Feb 20, 2004, Bryan W. Headley.
 *      v1.5 - Added previousJitterable, so we don't do jitter delay when the
 *             user is holding a button down for periods of time.
 *
 * NOTE:
 *      This kernel driver is augmented by the "Aiptek" XFree86 input
 *      driver for your X server, as well as the Gaiptek GUI Front-end
 *      "Tablet Manager".
 *      These three products are highly interactive with one another,
 *      so therefore it's easier to document them all as one subsystem.
 *      Please visit the project's "home page", located at,
 *      http://aiptektablet.sourceforge.net.
 */

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

/*
 * Aiptek status packet:
 *
 * (returned as Report 1 - relative coordinates from mouse and stylus)
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     0     0     1
 * byte1   0     0     0     0     0    BS2   BS    Tip
 * byte2  X7    X6    X5    X4    X3    X2    X1    X0
 * byte3  Y7    Y6    Y5    Y4    Y3    Y2    Y1    Y0
 *
 * (returned as Report 2 - absolute coordinates from the stylus)
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     0     1     0
 * byte1  X7    X6    X5    X4    X3    X2    X1    X0
 * byte2  X15   X14   X13   X12   X11   X10   X9    X8
 * byte3  Y7    Y6    Y5    Y4    Y3    Y2    Y1    Y0
 * byte4  Y15   Y14   Y13   Y12   Y11   Y10   Y9    Y8
 * byte5   *     *     *    BS2   BS1   Tip   IR    DV
 * byte6  P7    P6    P5    P4    P3    P2    P1    P0
 * byte7  P15   P14   P13   P12   P11   P10   P9    P8
 *
 * (returned as Report 3 - absolute coordinates from the mouse)
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     0     1     1
 * byte1  X7    X6    X5    X4    X3    X2    X1    X0
 * byte2  X15   X14   X13   X12   X11   X10   X9    X8
 * byte3  Y7    Y6    Y5    Y4    Y3    Y2    Y1    Y0
 * byte4  Y15   Y14   Y13   Y12   Y11   Y10   Y9    Y8
 * byte5   *     *     *    BS2   BS1   Tip   IR    DV
 * byte6  P7    P6    P5    P4    P3    P2    P1    P0
 * byte7  P15   P14   P13   P12   P11   P10   P9    P8
 *
 * (returned as Report 4 - macrokeys from the stylus)
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     1     0     0
 * byte1   0     0     0    BS2   BS    Tip   IR    DV
 * byte2   0     0     0     0     0     0     1     0
 * byte3   0     0     0    K4    K3    K2    K1    K0
 * byte4  P7    P6    P5    P4    P3    P2    P1    P0
 * byte5  P15   P14   P13   P12   P11   P10   P9    P8
 *
 * (returned as Report 5 - macrokeys from the mouse)
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     1     0     1
 * byte1   0     0     0    BS2   BS    Tip   IR    DV
 * byte2   0     0     0     0     0     0     1     0
 * byte3   0     0     0    K4    K3    K2    K1    K0
 * byte4  P7    P6    P5    P4    P3    P2    P1    P0
 * byte5  P15   P14   P13   P12   P11   P10   P9    P8
 *
 * IR: In Range = Proximity on
 * DV = Data Valid
 * BS = Barrel Switch (as in, macro keys)
 * BS2 also referred to as Tablet Pick
 *
 * Command Summary:
 *
 * Use report_type CONTROL (3)
 * Use report_id   2
 *
 * Command/Data    Description     Return Bytes    Return Value
 * 0x10/0x00       SwitchToMouse       0
 * 0x10/0x01       SwitchToTablet      0
 * 0x18/0x04       SetResolution       0
 * 0x12/0xFF       AutoGainOn          0
 * 0x17/0x00       FilterOn            0
 * 0x01/0x00       GetXExtension       2           MaxX
 * 0x01/0x01       GetYExtension       2           MaxY
 * 0x02/0x00       GetModelCode        2           ModelCode = LOBYTE
 * 0x03/0x00       GetODMCode          2           ODMCode
 * 0x08/0x00       GetPressureLevels   2           =512
 * 0x04/0x00       GetFirmwareVersion  2           Firmware Version
 * 0x11/0x02       EnableMacroKeys     0
 *
 * To initialize the tablet:
 *
 * (1) Send Resolution500LPI (Command)
 * (2) Query for Model code (Option Report)
 * (3) Query for ODM code (Option Report)
 * (4) Query for firmware (Option Report)
 * (5) Query for GetXExtension (Option Report)
 * (6) Query for GetYExtension (Option Report)
 * (7) Query for GetPressureLevels (Option Report)
 * (8) SwitchToTablet for Absolute coordinates, or
 *     SwitchToMouse for Relative coordinates (Command)
 * (9) EnableMacroKeys (Command)
 * (10) FilterOn (Command)
 * (11) AutoGainOn (Command)
 *
 * (Step 9 can be omitted, but you'll then have no function keys.)
 */

#define USB_VENDOR_ID_AIPTEK				0x08ca
#define USB_VENDOR_ID_KYE				0x0458
#define USB_REQ_GET_REPORT				0x01
#define USB_REQ_SET_REPORT				0x09

	/* PointerMode codes
	 */
#define AIPTEK_POINTER_ONLY_MOUSE_MODE			0
#define AIPTEK_POINTER_ONLY_STYLUS_MODE			1
#define AIPTEK_POINTER_EITHER_MODE			2

#define AIPTEK_POINTER_ALLOW_MOUSE_MODE(a)		\
	(a == AIPTEK_POINTER_ONLY_MOUSE_MODE ||		\
	 a == AIPTEK_POINTER_EITHER_MODE)
#define AIPTEK_POINTER_ALLOW_STYLUS_MODE(a)		\
	(a == AIPTEK_POINTER_ONLY_STYLUS_MODE ||	\
	 a == AIPTEK_POINTER_EITHER_MODE)

	/* CoordinateMode code
	 */
#define AIPTEK_COORDINATE_RELATIVE_MODE			0
#define AIPTEK_COORDINATE_ABSOLUTE_MODE			1

       /* XTilt and YTilt values
        */
#define AIPTEK_TILT_MIN					(-128)
#define AIPTEK_TILT_MAX					127
#define AIPTEK_TILT_DISABLE				(-10101)

	/* Wheel values
	 */
#define AIPTEK_WHEEL_MIN				0
#define AIPTEK_WHEEL_MAX				1024
#define AIPTEK_WHEEL_DISABLE				(-10101)

	/* ToolCode values, which BTW are 0x140 .. 0x14f
	 * We have things set up such that if the tool button has changed,
	 * the tools get reset.
	 */
	/* toolMode codes
	 */
#define AIPTEK_TOOL_BUTTON_PEN_MODE			BTN_TOOL_PEN
#define AIPTEK_TOOL_BUTTON_PENCIL_MODE			BTN_TOOL_PENCIL
#define AIPTEK_TOOL_BUTTON_BRUSH_MODE			BTN_TOOL_BRUSH
#define AIPTEK_TOOL_BUTTON_AIRBRUSH_MODE		BTN_TOOL_AIRBRUSH
#define AIPTEK_TOOL_BUTTON_ERASER_MODE			BTN_TOOL_RUBBER
#define AIPTEK_TOOL_BUTTON_MOUSE_MODE			BTN_TOOL_MOUSE
#define AIPTEK_TOOL_BUTTON_LENS_MODE			BTN_TOOL_LENS

	/* Diagnostic message codes
	 */
#define AIPTEK_DIAGNOSTIC_NA				0
#define AIPTEK_DIAGNOSTIC_SENDING_RELATIVE_IN_ABSOLUTE	1
#define AIPTEK_DIAGNOSTIC_SENDING_ABSOLUTE_IN_RELATIVE	2
#define AIPTEK_DIAGNOSTIC_TOOL_DISALLOWED		3

	/* Time to wait (in ms) to help mask hand jittering
	 * when pressing the stylus buttons.
	 */
#define AIPTEK_JITTER_DELAY_DEFAULT			50

	/* Time to wait (in ms) in-between sending the tablet
	 * a command and beginning the process of reading the return
	 * sequence from the tablet.
	 */
#define AIPTEK_PROGRAMMABLE_DELAY_25		25
#define AIPTEK_PROGRAMMABLE_DELAY_50		50
#define AIPTEK_PROGRAMMABLE_DELAY_100		100
#define AIPTEK_PROGRAMMABLE_DELAY_200		200
#define AIPTEK_PROGRAMMABLE_DELAY_300		300
#define AIPTEK_PROGRAMMABLE_DELAY_400		400
#define AIPTEK_PROGRAMMABLE_DELAY_DEFAULT	AIPTEK_PROGRAMMABLE_DELAY_400

	/* Mouse button programming
	 */
#define AIPTEK_MOUSE_LEFT_BUTTON		0x04
#define AIPTEK_MOUSE_RIGHT_BUTTON		0x08
#define AIPTEK_MOUSE_MIDDLE_BUTTON		0x10

	/* Stylus button programming
	 */
#define AIPTEK_STYLUS_LOWER_BUTTON		0x08
#define AIPTEK_STYLUS_UPPER_BUTTON		0x10

	/* Length of incoming packet from the tablet
	 */
#define AIPTEK_PACKET_LENGTH			8

	/* We report in EV_MISC both the proximity and
	 * whether the report came from the stylus, tablet mouse
	 * or "unknown" -- Unknown when the tablet is in relative
	 * mode, because we only get report 1's.
	 */
#define AIPTEK_REPORT_TOOL_UNKNOWN		0x10
#define AIPTEK_REPORT_TOOL_STYLUS		0x20
#define AIPTEK_REPORT_TOOL_MOUSE		0x40

static int programmableDelay = AIPTEK_PROGRAMMABLE_DELAY_DEFAULT;
static int jitterDelay = AIPTEK_JITTER_DELAY_DEFAULT;

struct aiptek_features {
	int odmCode;		/* Tablet manufacturer code       */
	int modelCode;		/* Tablet model code (not unique) */
	int firmwareCode;	/* prom/eeprom version            */
	char usbPath[64 + 1];	/* device's physical usb path     */
};

struct aiptek_settings {
	int pointerMode;	/* stylus-, mouse-only or either */
	int coordinateMode;	/* absolute/relative coords      */
	int toolMode;		/* pen, pencil, brush, etc. tool */
	int xTilt;		/* synthetic xTilt amount        */
	int yTilt;		/* synthetic yTilt amount        */
	int wheel;		/* synthetic wheel amount        */
	int stylusButtonUpper;	/* stylus upper btn delivers...  */
	int stylusButtonLower;	/* stylus lower btn delivers...  */
	int mouseButtonLeft;	/* mouse left btn delivers...    */
	int mouseButtonMiddle;	/* mouse middle btn delivers...  */
	int mouseButtonRight;	/* mouse right btn delivers...   */
	int programmableDelay;	/* delay for tablet programming  */
	int jitterDelay;	/* delay for hand jittering      */
};

struct aiptek {
	struct input_dev *inputdev;		/* input device struct           */
	struct usb_interface *intf;		/* usb interface struct          */
	struct urb *urb;			/* urb for incoming reports      */
	dma_addr_t data_dma;			/* our dma stuffage              */
	struct aiptek_features features;	/* tablet's array of features    */
	struct aiptek_settings curSetting;	/* tablet's current programmable */
	struct aiptek_settings newSetting;	/* ... and new param settings    */
	unsigned int ifnum;			/* interface number for IO       */
	int diagnostic;				/* tablet diagnostic codes       */
	unsigned long eventCount;		/* event count                   */
	int inDelay;				/* jitter: in jitter delay?      */
	unsigned long endDelay;			/* jitter: time when delay ends  */
	int previousJitterable;			/* jitterable prev value     */

	int lastMacro;				/* macro key to reset            */
	int previousToolMode;			/* pen, pencil, brush, etc. tool */
	unsigned char *data;			/* incoming packet data          */
};

static const int eventTypes[] = {
        EV_KEY, EV_ABS, EV_REL, EV_MSC,
};

static const int absEvents[] = {
        ABS_X, ABS_Y, ABS_PRESSURE, ABS_TILT_X, ABS_TILT_Y,
        ABS_WHEEL, ABS_MISC,
};

static const int relEvents[] = {
        REL_X, REL_Y, REL_WHEEL,
};

static const int buttonEvents[] = {
	BTN_LEFT, BTN_RIGHT, BTN_MIDDLE,
	BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_PENCIL, BTN_TOOL_AIRBRUSH,
	BTN_TOOL_BRUSH, BTN_TOOL_MOUSE, BTN_TOOL_LENS, BTN_TOUCH,
	BTN_STYLUS, BTN_STYLUS2,
};

/*
 * Permit easy lookup of keyboard events to send, versus
 * the bitmap which comes from the tablet. This hides the
 * issue that the F_keys are not sequentially numbered.
 */
static const int macroKeyEvents[] = {
	KEY_ESC, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
	KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11,
	KEY_F12, KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17,
	KEY_F18, KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23,
	KEY_F24, KEY_STOP, KEY_AGAIN, KEY_PROPS, KEY_UNDO,
	KEY_FRONT, KEY_COPY, KEY_OPEN, KEY_PASTE, 0
};

/***********************************************************************
 * Map values to strings and back. Every map should have the following
 * as its last element: { NULL, AIPTEK_INVALID_VALUE }.
 */
#define AIPTEK_INVALID_VALUE	-1

struct aiptek_map {
	const char *string;
	int value;
};

static int map_str_to_val(const struct aiptek_map *map, const char *str, size_t count)
{
	const struct aiptek_map *p;

	if (str[count - 1] == '\n')
		count--;

	for (p = map; p->string; p++)
	        if (!strncmp(str, p->string, count))
			return p->value;

	return AIPTEK_INVALID_VALUE;
}

static const char *map_val_to_str(const struct aiptek_map *map, int val)
{
	const struct aiptek_map *p;

	for (p = map; p->value != AIPTEK_INVALID_VALUE; p++)
		if (val == p->value)
			return p->string;

	return "unknown";
}

/***********************************************************************
 * aiptek_irq can receive one of six potential reports.
 * The documentation for each is in the body of the function.
 *
 * The tablet reports on several attributes per invocation of
 * aiptek_irq. Because the Linux Input Event system allows the
 * transmission of ONE attribute per input_report_xxx() call,
 * collation has to be done on the other end to reconstitute
 * a complete tablet report. Further, the number of Input Event reports
 * submitted varies, depending on what USB report type, and circumstance.
 * To deal with this, EV_MSC is used to indicate an 'end-of-report'
 * message. This has been an undocumented convention understood by the kernel
 * tablet driver and clients such as gpm and XFree86's tablet drivers.
 *
 * Of the information received from the tablet, the one piece I
 * cannot transmit is the proximity bit (without resorting to an EV_MSC
 * convention above.) I therefore have taken over REL_MISC and ABS_MISC
 * (for relative and absolute reports, respectively) for communicating
 * Proximity. Why two events? I thought it interesting to know if the
 * Proximity event occurred while the tablet was in absolute or relative
 * mode.
 * Update: REL_MISC proved not to be such a good idea. With REL_MISC you
 * get an event transmitted each time. ABS_MISC works better, since it
 * can be set and re-set. Thus, only using ABS_MISC from now on.
 *
 * Other tablets use the notion of a certain minimum stylus pressure
 * to infer proximity. While that could have been done, that is yet
 * another 'by convention' behavior, the documentation for which
 * would be spread between two (or more) pieces of software.
 *
 * EV_MSC usage was terminated for this purpose in Linux 2.5.x, and
 * replaced with the input_sync() method (which emits EV_SYN.)
 */

static void aiptek_irq(struct urb *urb)
{
	struct aiptek *aiptek = urb->context;
	unsigned char *data = aiptek->data;
	struct input_dev *inputdev = aiptek->inputdev;
	struct usb_interface *intf = aiptek->intf;
	int jitterable = 0;
	int retval, macro, x, y, z, left, right, middle, p, dv, tip, bs, pck;

	switch (urb->status) {
	case 0:
		/* Success */
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* This urb is terminated, clean up */
		dev_dbg(&intf->dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;

	default:
		dev_dbg(&intf->dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}

	/* See if we are in a delay loop -- throw out report if true.
	 */
	if (aiptek->inDelay == 1 && time_after(aiptek->endDelay, jiffies)) {
		goto exit;
	}

	aiptek->inDelay = 0;
	aiptek->eventCount++;

	/* Report 1 delivers relative coordinates with either a stylus
	 * or the mouse. You do not know, however, which input
	 * tool generated the event.
	 */
	if (data[0] == 1) {
		if (aiptek->curSetting.coordinateMode ==
		    AIPTEK_COORDINATE_ABSOLUTE_MODE) {
			aiptek->diagnostic =
			    AIPTEK_DIAGNOSTIC_SENDING_RELATIVE_IN_ABSOLUTE;
		} else {
			x = (signed char) data[2];
			y = (signed char) data[3];

			/* jitterable keeps track of whether any button has been pressed.
			 * We're also using it to remap the physical mouse button mask
			 * to pseudo-settings. (We don't specifically care about it's
			 * value after moving/transposing mouse button bitmasks, except
			 * that a non-zero value indicates that one or more
			 * mouse button was pressed.)
			 */
			jitterable = data[1] & 0x07;

			left = (data[1] & aiptek->curSetting.mouseButtonLeft >> 2) != 0 ? 1 : 0;
			right = (data[1] & aiptek->curSetting.mouseButtonRight >> 2) != 0 ? 1 : 0;
			middle = (data[1] & aiptek->curSetting.mouseButtonMiddle >> 2) != 0 ? 1 : 0;

			input_report_key(inputdev, BTN_LEFT, left);
			input_report_key(inputdev, BTN_MIDDLE, middle);
			input_report_key(inputdev, BTN_RIGHT, right);

			input_report_abs(inputdev, ABS_MISC,
					 1 | AIPTEK_REPORT_TOOL_UNKNOWN);
			input_report_rel(inputdev, REL_X, x);
			input_report_rel(inputdev, REL_Y, y);

			/* Wheel support is in the form of a single-event
			 * firing.
			 */
			if (aiptek->curSetting.wheel != AIPTEK_WHEEL_DISABLE) {
				input_report_rel(inputdev, REL_WHEEL,
						 aiptek->curSetting.wheel);
				aiptek->curSetting.wheel = AIPTEK_WHEEL_DISABLE;
			}
			if (aiptek->lastMacro != -1) {
			        input_report_key(inputdev,
						 macroKeyEvents[aiptek->lastMacro], 0);
				aiptek->lastMacro = -1;
			}
			input_sync(inputdev);
		}
	}
	/* Report 2 is delivered only by the stylus, and delivers
	 * absolute coordinates.
	 */
	else if (data[0] == 2) {
		if (aiptek->curSetting.coordinateMode == AIPTEK_COORDINATE_RELATIVE_MODE) {
			aiptek->diagnostic = AIPTEK_DIAGNOSTIC_SENDING_ABSOLUTE_IN_RELATIVE;
		} else if (!AIPTEK_POINTER_ALLOW_STYLUS_MODE
			    (aiptek->curSetting.pointerMode)) {
				aiptek->diagnostic = AIPTEK_DIAGNOSTIC_TOOL_DISALLOWED;
		} else {
			x = get_unaligned_le16(data + 1);
			y = get_unaligned_le16(data + 3);
			z = get_unaligned_le16(data + 6);

			dv = (data[5] & 0x01) != 0 ? 1 : 0;
			p = (data[5] & 0x02) != 0 ? 1 : 0;
			tip = (data[5] & 0x04) != 0 ? 1 : 0;

			/* Use jitterable to re-arrange button masks
			 */
			jitterable = data[5] & 0x18;

			bs = (data[5] & aiptek->curSetting.stylusButtonLower) != 0 ? 1 : 0;
			pck = (data[5] & aiptek->curSetting.stylusButtonUpper) != 0 ? 1 : 0;

			/* dv indicates 'data valid' (e.g., the tablet is in sync
			 * and has delivered a "correct" report) We will ignore
			 * all 'bad' reports...
			 */
			if (dv != 0) {
				/* If the selected tool changed, reset the old
				 * tool key, and set the new one.
				 */
				if (aiptek->previousToolMode !=
				    aiptek->curSetting.toolMode) {
				        input_report_key(inputdev,
							 aiptek->previousToolMode, 0);
					input_report_key(inputdev,
							 aiptek->curSetting.toolMode,
							 1);
					aiptek->previousToolMode =
					          aiptek->curSetting.toolMode;
				}

				if (p != 0) {
					input_report_abs(inputdev, ABS_X, x);
					input_report_abs(inputdev, ABS_Y, y);
					input_report_abs(inputdev, ABS_PRESSURE, z);

					input_report_key(inputdev, BTN_TOUCH, tip);
					input_report_key(inputdev, BTN_STYLUS, bs);
					input_report_key(inputdev, BTN_STYLUS2, pck);

					if (aiptek->curSetting.xTilt !=
					    AIPTEK_TILT_DISABLE) {
						input_report_abs(inputdev,
								 ABS_TILT_X,
								 aiptek->curSetting.xTilt);
					}
					if (aiptek->curSetting.yTilt != AIPTEK_TILT_DISABLE) {
						input_report_abs(inputdev,
								 ABS_TILT_Y,
								 aiptek->curSetting.yTilt);
					}

					/* Wheel support is in the form of a single-event
					 * firing.
					 */
					if (aiptek->curSetting.wheel !=
					    AIPTEK_WHEEL_DISABLE) {
						input_report_abs(inputdev,
								 ABS_WHEEL,
								 aiptek->curSetting.wheel);
						aiptek->curSetting.wheel = AIPTEK_WHEEL_DISABLE;
					}
				}
				input_report_abs(inputdev, ABS_MISC, p | AIPTEK_REPORT_TOOL_STYLUS);
				if (aiptek->lastMacro != -1) {
			                input_report_key(inputdev,
							 macroKeyEvents[aiptek->lastMacro], 0);
					aiptek->lastMacro = -1;
				}
				input_sync(inputdev);
			}
		}
	}
	/* Report 3's come from the mouse in absolute mode.
	 */
	else if (data[0] == 3) {
		if (aiptek->curSetting.coordinateMode == AIPTEK_COORDINATE_RELATIVE_MODE) {
			aiptek->diagnostic = AIPTEK_DIAGNOSTIC_SENDING_ABSOLUTE_IN_RELATIVE;
		} else if (!AIPTEK_POINTER_ALLOW_MOUSE_MODE
			(aiptek->curSetting.pointerMode)) {
			aiptek->diagnostic = AIPTEK_DIAGNOSTIC_TOOL_DISALLOWED;
		} else {
			x = get_unaligned_le16(data + 1);
			y = get_unaligned_le16(data + 3);

			jitterable = data[5] & 0x1c;

			dv = (data[5] & 0x01) != 0 ? 1 : 0;
			p = (data[5] & 0x02) != 0 ? 1 : 0;
			left = (data[5] & aiptek->curSetting.mouseButtonLeft) != 0 ? 1 : 0;
			right = (data[5] & aiptek->curSetting.mouseButtonRight) != 0 ? 1 : 0;
			middle = (data[5] & aiptek->curSetting.mouseButtonMiddle) != 0 ? 1 : 0;

			if (dv != 0) {
				/* If the selected tool changed, reset the old
				 * tool key, and set the new one.
				 */
				if (aiptek->previousToolMode !=
				    aiptek->curSetting.toolMode) {
				        input_report_key(inputdev,
							 aiptek->previousToolMode, 0);
					input_report_key(inputdev,
							 aiptek->curSetting.toolMode,
							 1);
					aiptek->previousToolMode =
					          aiptek->curSetting.toolMode;
				}

				if (p != 0) {
					input_report_abs(inputdev, ABS_X, x);
					input_report_abs(inputdev, ABS_Y, y);

					input_report_key(inputdev, BTN_LEFT, left);
					input_report_key(inputdev, BTN_MIDDLE, middle);
					input_report_key(inputdev, BTN_RIGHT, right);

					/* Wheel support is in the form of a single-event
					 * firing.
					 */
					if (aiptek->curSetting.wheel != AIPTEK_WHEEL_DISABLE) {
						input_report_abs(inputdev,
								 ABS_WHEEL,
								 aiptek->curSetting.wheel);
						aiptek->curSetting.wheel = AIPTEK_WHEEL_DISABLE;
					}
				}
				input_report_abs(inputdev, ABS_MISC, p | AIPTEK_REPORT_TOOL_MOUSE);
				if (aiptek->lastMacro != -1) {
			                input_report_key(inputdev,
							 macroKeyEvents[aiptek->lastMacro], 0);
				        aiptek->lastMacro = -1;
				}
				input_sync(inputdev);
			}
		}
	}
	/* Report 4s come from the macro keys when pressed by stylus
	 */
	else if (data[0] == 4) {
		jitterable = data[1] & 0x18;

		dv = (data[1] & 0x01) != 0 ? 1 : 0;
		p = (data[1] & 0x02) != 0 ? 1 : 0;
		tip = (data[1] & 0x04) != 0 ? 1 : 0;
		bs = (data[1] & aiptek->curSetting.stylusButtonLower) != 0 ? 1 : 0;
		pck = (data[1] & aiptek->curSetting.stylusButtonUpper) != 0 ? 1 : 0;

		macro = dv && p && tip && !(data[3] & 1) ? (data[3] >> 1) : -1;
		z = get_unaligned_le16(data + 4);

		if (dv) {
		        /* If the selected tool changed, reset the old
			 * tool key, and set the new one.
			 */
		        if (aiptek->previousToolMode !=
			    aiptek->curSetting.toolMode) {
			        input_report_key(inputdev,
						 aiptek->previousToolMode, 0);
				input_report_key(inputdev,
						 aiptek->curSetting.toolMode,
						 1);
				aiptek->previousToolMode =
				        aiptek->curSetting.toolMode;
			}
		}

		if (aiptek->lastMacro != -1 && aiptek->lastMacro != macro) {
		        input_report_key(inputdev, macroKeyEvents[aiptek->lastMacro], 0);
			aiptek->lastMacro = -1;
		}

		if (macro != -1 && macro != aiptek->lastMacro) {
			input_report_key(inputdev, macroKeyEvents[macro], 1);
			aiptek->lastMacro = macro;
		}
		input_report_abs(inputdev, ABS_MISC,
				 p | AIPTEK_REPORT_TOOL_STYLUS);
		input_sync(inputdev);
	}
	/* Report 5s come from the macro keys when pressed by mouse
	 */
	else if (data[0] == 5) {
		jitterable = data[1] & 0x1c;

		dv = (data[1] & 0x01) != 0 ? 1 : 0;
		p = (data[1] & 0x02) != 0 ? 1 : 0;
		left = (data[1]& aiptek->curSetting.mouseButtonLeft) != 0 ? 1 : 0;
		right = (data[1] & aiptek->curSetting.mouseButtonRight) != 0 ? 1 : 0;
		middle = (data[1] & aiptek->curSetting.mouseButtonMiddle) != 0 ? 1 : 0;
		macro = dv && p && left && !(data[3] & 1) ? (data[3] >> 1) : 0;

		if (dv) {
		        /* If the selected tool changed, reset the old
			 * tool key, and set the new one.
			 */
		        if (aiptek->previousToolMode !=
			    aiptek->curSetting.toolMode) {
		                input_report_key(inputdev,
						 aiptek->previousToolMode, 0);
			        input_report_key(inputdev,
						 aiptek->curSetting.toolMode, 1);
			        aiptek->previousToolMode = aiptek->curSetting.toolMode;
			}
		}

		if (aiptek->lastMacro != -1 && aiptek->lastMacro != macro) {
		        input_report_key(inputdev, macroKeyEvents[aiptek->lastMacro], 0);
			aiptek->lastMacro = -1;
		}

		if (macro != -1 && macro != aiptek->lastMacro) {
			input_report_key(inputdev, macroKeyEvents[macro], 1);
			aiptek->lastMacro = macro;
		}

		input_report_abs(inputdev, ABS_MISC,
				 p | AIPTEK_REPORT_TOOL_MOUSE);
		input_sync(inputdev);
	}
	/* We have no idea which tool can generate a report 6. Theoretically,
	 * neither need to, having been given reports 4 & 5 for such use.
	 * However, report 6 is the 'official-looking' report for macroKeys;
	 * reports 4 & 5 supposively are used to support unnamed, unknown
	 * hat switches (which just so happen to be the macroKeys.)
	 */
	else if (data[0] == 6) {
		macro = get_unaligned_le16(data + 1);
		if (macro > 0) {
			input_report_key(inputdev, macroKeyEvents[macro - 1],
					 0);
		}
		if (macro < 25) {
			input_report_key(inputdev, macroKeyEvents[macro + 1],
					 0);
		}

		/* If the selected tool changed, reset the old
		   tool key, and set the new one.
		*/
		if (aiptek->previousToolMode !=
		    aiptek->curSetting.toolMode) {
		        input_report_key(inputdev,
					 aiptek->previousToolMode, 0);
			input_report_key(inputdev,
					 aiptek->curSetting.toolMode,
					 1);
			aiptek->previousToolMode =
				aiptek->curSetting.toolMode;
		}

		input_report_key(inputdev, macroKeyEvents[macro], 1);
		input_report_abs(inputdev, ABS_MISC,
				 1 | AIPTEK_REPORT_TOOL_UNKNOWN);
		input_sync(inputdev);
	} else {
		dev_dbg(&intf->dev, "Unknown report %d\n", data[0]);
	}

	/* Jitter may occur when the user presses a button on the stlyus
	 * or the mouse. What we do to prevent that is wait 'x' milliseconds
	 * following a 'jitterable' event, which should give the hand some time
	 * stabilize itself.
	 *
	 * We just introduced aiptek->previousJitterable to carry forth the
	 * notion that jitter occurs when the button state changes from on to off:
	 * a person drawing, holding a button down is not subject to jittering.
	 * With that in mind, changing from upper button depressed to lower button
	 * WILL transition through a jitter delay.
	 */

	if (aiptek->previousJitterable != jitterable &&
	    aiptek->curSetting.jitterDelay != 0 && aiptek->inDelay != 1) {
		aiptek->endDelay = jiffies +
		    ((aiptek->curSetting.jitterDelay * HZ) / 1000);
		aiptek->inDelay = 1;
	}
	aiptek->previousJitterable = jitterable;

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval != 0) {
		dev_err(&intf->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
	}
}

/***********************************************************************
 * These are the USB id's known so far. We do not identify them to
 * specific Aiptek model numbers, because there has been overlaps,
 * use, and reuse of id's in existing models. Certain models have
 * been known to use more than one ID, indicative perhaps of
 * manufacturing revisions. In any event, we consider these
 * IDs to not be model-specific nor unique.
 */
static const struct usb_device_id aiptek_ids[] = {
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x01)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x10)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x20)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x21)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x22)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x23)},
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x24)},
	{USB_DEVICE(USB_VENDOR_ID_KYE, 0x5003)},
	{}
};

MODULE_DEVICE_TABLE(usb, aiptek_ids);

/***********************************************************************
 * Open an instance of the tablet driver.
 */
static int aiptek_open(struct input_dev *inputdev)
{
	struct aiptek *aiptek = input_get_drvdata(inputdev);

	aiptek->urb->dev = interface_to_usbdev(aiptek->intf);
	if (usb_submit_urb(aiptek->urb, GFP_KERNEL) != 0)
		return -EIO;

	return 0;
}

/***********************************************************************
 * Close an instance of the tablet driver.
 */
static void aiptek_close(struct input_dev *inputdev)
{
	struct aiptek *aiptek = input_get_drvdata(inputdev);

	usb_kill_urb(aiptek->urb);
}

/***********************************************************************
 * aiptek_set_report and aiptek_get_report() are borrowed from Linux 2.4.x,
 * where they were known as usb_set_report and usb_get_report.
 */
static int
aiptek_set_report(struct aiptek *aiptek,
		  unsigned char report_type,
		  unsigned char report_id, void *buffer, int size)
{
	struct usb_device *udev = interface_to_usbdev(aiptek->intf);

	return usb_control_msg(udev,
			       usb_sndctrlpipe(udev, 0),
			       USB_REQ_SET_REPORT,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE |
			       USB_DIR_OUT, (report_type << 8) + report_id,
			       aiptek->ifnum, buffer, size, 5000);
}

static int
aiptek_get_report(struct aiptek *aiptek,
		  unsigned char report_type,
		  unsigned char report_id, void *buffer, int size)
{
	struct usb_device *udev = interface_to_usbdev(aiptek->intf);

	return usb_control_msg(udev,
			       usb_rcvctrlpipe(udev, 0),
			       USB_REQ_GET_REPORT,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE |
			       USB_DIR_IN, (report_type << 8) + report_id,
			       aiptek->ifnum, buffer, size, 5000);
}

/***********************************************************************
 * Send a command to the tablet.
 */
static int
aiptek_command(struct aiptek *aiptek, unsigned char command, unsigned char data)
{
	const int sizeof_buf = 3 * sizeof(u8);
	int ret;
	u8 *buf;

	buf = kmalloc(sizeof_buf, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = 2;
	buf[1] = command;
	buf[2] = data;

	if ((ret =
	     aiptek_set_report(aiptek, 3, 2, buf, sizeof_buf)) != sizeof_buf) {
		dev_dbg(&aiptek->intf->dev,
			"aiptek_program: failed, tried to send: 0x%02x 0x%02x\n",
			command, data);
	}
	kfree(buf);
	return ret < 0 ? ret : 0;
}

/***********************************************************************
 * Retrieve information from the tablet. Querying info is defined as first
 * sending the {command,data} sequence as a command, followed by a wait
 * (aka, "programmaticDelay") and then a "read" request.
 */
static int
aiptek_query(struct aiptek *aiptek, unsigned char command, unsigned char data)
{
	const int sizeof_buf = 3 * sizeof(u8);
	int ret;
	u8 *buf;

	buf = kmalloc(sizeof_buf, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = 2;
	buf[1] = command;
	buf[2] = data;

	if (aiptek_command(aiptek, command, data) != 0) {
		kfree(buf);
		return -EIO;
	}
	msleep(aiptek->curSetting.programmableDelay);

	if ((ret =
	     aiptek_get_report(aiptek, 3, 2, buf, sizeof_buf)) != sizeof_buf) {
		dev_dbg(&aiptek->intf->dev,
			"aiptek_query failed: returned 0x%02x 0x%02x 0x%02x\n",
			buf[0], buf[1], buf[2]);
		ret = -EIO;
	} else {
		ret = get_unaligned_le16(buf + 1);
	}
	kfree(buf);
	return ret;
}

/***********************************************************************
 * Program the tablet into either absolute or relative mode.
 * We also get information about the tablet's size.
 */
static int aiptek_program_tablet(struct aiptek *aiptek)
{
	int ret;
	/* Execute Resolution500LPI */
	if ((ret = aiptek_command(aiptek, 0x18, 0x04)) < 0)
		return ret;

	/* Query getModelCode */
	if ((ret = aiptek_query(aiptek, 0x02, 0x00)) < 0)
		return ret;
	aiptek->features.modelCode = ret & 0xff;

	/* Query getODMCode */
	if ((ret = aiptek_query(aiptek, 0x03, 0x00)) < 0)
		return ret;
	aiptek->features.odmCode = ret;

	/* Query getFirmwareCode */
	if ((ret = aiptek_query(aiptek, 0x04, 0x00)) < 0)
		return ret;
	aiptek->features.firmwareCode = ret;

	/* Query getXextension */
	if ((ret = aiptek_query(aiptek, 0x01, 0x00)) < 0)
		return ret;
	input_set_abs_params(aiptek->inputdev, ABS_X, 0, ret - 1, 0, 0);

	/* Query getYextension */
	if ((ret = aiptek_query(aiptek, 0x01, 0x01)) < 0)
		return ret;
	input_set_abs_params(aiptek->inputdev, ABS_Y, 0, ret - 1, 0, 0);

	/* Query getPressureLevels */
	if ((ret = aiptek_query(aiptek, 0x08, 0x00)) < 0)
		return ret;
	input_set_abs_params(aiptek->inputdev, ABS_PRESSURE, 0, ret - 1, 0, 0);

	/* Depending on whether we are in absolute or relative mode, we will
	 * do a switchToTablet(absolute) or switchToMouse(relative) command.
	 */
	if (aiptek->curSetting.coordinateMode ==
	    AIPTEK_COORDINATE_ABSOLUTE_MODE) {
		/* Execute switchToTablet */
		if ((ret = aiptek_command(aiptek, 0x10, 0x01)) < 0) {
			return ret;
		}
	} else {
		/* Execute switchToMouse */
		if ((ret = aiptek_command(aiptek, 0x10, 0x00)) < 0) {
			return ret;
		}
	}

	/* Enable the macro keys */
	if ((ret = aiptek_command(aiptek, 0x11, 0x02)) < 0)
		return ret;
#if 0
	/* Execute FilterOn */
	if ((ret = aiptek_command(aiptek, 0x17, 0x00)) < 0)
		return ret;
#endif

	/* Execute AutoGainOn */
	if ((ret = aiptek_command(aiptek, 0x12, 0xff)) < 0)
		return ret;

	/* Reset the eventCount, so we track events from last (re)programming
	 */
	aiptek->diagnostic = AIPTEK_DIAGNOSTIC_NA;
	aiptek->eventCount = 0;

	return 0;
}

/***********************************************************************
 * Sysfs functions. Sysfs prefers that individually-tunable parameters
 * exist in their separate pseudo-files. Summary data that is immutable
 * may exist in a singular file so long as you don't define a writeable
 * interface.
 */

/***********************************************************************
 * support the 'size' file -- display support
 */
static ssize_t show_tabletSize(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%dx%d\n",
			  input_abs_get_max(aiptek->inputdev, ABS_X) + 1,
			  input_abs_get_max(aiptek->inputdev, ABS_Y) + 1);
}

/* These structs define the sysfs files, param #1 is the name of the
 * file, param 2 is the file permissions, param 3 & 4 are to the
 * output generator and input parser routines. Absence of a routine is
 * permitted -- it only means can't either 'cat' the file, or send data
 * to it.
 */
static DEVICE_ATTR(size, S_IRUGO, show_tabletSize, NULL);

/***********************************************************************
 * support routines for the 'pointer_mode' file. Note that this file
 * both displays current setting and allows reprogramming.
 */
static struct aiptek_map pointer_mode_map[] = {
	{ "stylus",	AIPTEK_POINTER_ONLY_STYLUS_MODE },
	{ "mouse",	AIPTEK_POINTER_ONLY_MOUSE_MODE },
	{ "either",	AIPTEK_POINTER_EITHER_MODE },
	{ NULL,		AIPTEK_INVALID_VALUE }
};

static ssize_t show_tabletPointerMode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(pointer_mode_map,
						      aiptek->curSetting.pointerMode));
}

static ssize_t
store_tabletPointerMode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_mode = map_str_to_val(pointer_mode_map, buf, count);

	if (new_mode == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.pointerMode = new_mode;
	return count;
}

static DEVICE_ATTR(pointer_mode,
		   S_IRUGO | S_IWUSR,
		   show_tabletPointerMode, store_tabletPointerMode);

/***********************************************************************
 * support routines for the 'coordinate_mode' file. Note that this file
 * both displays current setting and allows reprogramming.
 */

static struct aiptek_map coordinate_mode_map[] = {
	{ "absolute",	AIPTEK_COORDINATE_ABSOLUTE_MODE },
	{ "relative",	AIPTEK_COORDINATE_RELATIVE_MODE },
	{ NULL,		AIPTEK_INVALID_VALUE }
};

static ssize_t show_tabletCoordinateMode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(coordinate_mode_map,
						      aiptek->curSetting.coordinateMode));
}

static ssize_t
store_tabletCoordinateMode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_mode = map_str_to_val(coordinate_mode_map, buf, count);

	if (new_mode == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.coordinateMode = new_mode;
	return count;
}

static DEVICE_ATTR(coordinate_mode,
		   S_IRUGO | S_IWUSR,
		   show_tabletCoordinateMode, store_tabletCoordinateMode);

/***********************************************************************
 * support routines for the 'tool_mode' file. Note that this file
 * both displays current setting and allows reprogramming.
 */

static struct aiptek_map tool_mode_map[] = {
	{ "mouse",	AIPTEK_TOOL_BUTTON_MOUSE_MODE },
	{ "eraser",	AIPTEK_TOOL_BUTTON_ERASER_MODE },
	{ "pencil",	AIPTEK_TOOL_BUTTON_PENCIL_MODE },
	{ "pen",	AIPTEK_TOOL_BUTTON_PEN_MODE },
	{ "brush",	AIPTEK_TOOL_BUTTON_BRUSH_MODE },
	{ "airbrush",	AIPTEK_TOOL_BUTTON_AIRBRUSH_MODE },
	{ "lens",	AIPTEK_TOOL_BUTTON_LENS_MODE },
	{ NULL,		AIPTEK_INVALID_VALUE }
};

static ssize_t show_tabletToolMode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(tool_mode_map,
						      aiptek->curSetting.toolMode));
}

static ssize_t
store_tabletToolMode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_mode = map_str_to_val(tool_mode_map, buf, count);

	if (new_mode == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.toolMode = new_mode;
	return count;
}

static DEVICE_ATTR(tool_mode,
		   S_IRUGO | S_IWUSR,
		   show_tabletToolMode, store_tabletToolMode);

/***********************************************************************
 * support routines for the 'xtilt' file. Note that this file
 * both displays current setting and allows reprogramming.
 */
static ssize_t show_tabletXtilt(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	if (aiptek->curSetting.xTilt == AIPTEK_TILT_DISABLE) {
		return sysfs_emit(buf, "disable\n");
	} else {
		return sysfs_emit(buf, "%d\n", aiptek->curSetting.xTilt);
	}
}

static ssize_t
store_tabletXtilt(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int x;

	if (kstrtoint(buf, 10, &x)) {
		size_t len = buf[count - 1] == '\n' ? count - 1 : count;

		if (strncmp(buf, "disable", len))
			return -EINVAL;

		aiptek->newSetting.xTilt = AIPTEK_TILT_DISABLE;
	} else {
		if (x < AIPTEK_TILT_MIN || x > AIPTEK_TILT_MAX)
			return -EINVAL;

		aiptek->newSetting.xTilt = x;
	}

	return count;
}

static DEVICE_ATTR(xtilt,
		   S_IRUGO | S_IWUSR, show_tabletXtilt, store_tabletXtilt);

/***********************************************************************
 * support routines for the 'ytilt' file. Note that this file
 * both displays current setting and allows reprogramming.
 */
static ssize_t show_tabletYtilt(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	if (aiptek->curSetting.yTilt == AIPTEK_TILT_DISABLE) {
		return sysfs_emit(buf, "disable\n");
	} else {
		return sysfs_emit(buf, "%d\n", aiptek->curSetting.yTilt);
	}
}

static ssize_t
store_tabletYtilt(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int y;

	if (kstrtoint(buf, 10, &y)) {
		size_t len = buf[count - 1] == '\n' ? count - 1 : count;

		if (strncmp(buf, "disable", len))
			return -EINVAL;

		aiptek->newSetting.yTilt = AIPTEK_TILT_DISABLE;
	} else {
		if (y < AIPTEK_TILT_MIN || y > AIPTEK_TILT_MAX)
			return -EINVAL;

		aiptek->newSetting.yTilt = y;
	}

	return count;
}

static DEVICE_ATTR(ytilt,
		   S_IRUGO | S_IWUSR, show_tabletYtilt, store_tabletYtilt);

/***********************************************************************
 * support routines for the 'jitter' file. Note that this file
 * both displays current setting and allows reprogramming.
 */
static ssize_t show_tabletJitterDelay(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", aiptek->curSetting.jitterDelay);
}

static ssize_t
store_tabletJitterDelay(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int err, j;

	err = kstrtoint(buf, 10, &j);
	if (err)
		return err;

	aiptek->newSetting.jitterDelay = j;
	return count;
}

static DEVICE_ATTR(jitter,
		   S_IRUGO | S_IWUSR,
		   show_tabletJitterDelay, store_tabletJitterDelay);

/***********************************************************************
 * support routines for the 'delay' file. Note that this file
 * both displays current setting and allows reprogramming.
 */
static ssize_t show_tabletProgrammableDelay(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", aiptek->curSetting.programmableDelay);
}

static ssize_t
store_tabletProgrammableDelay(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int err, d;

	err = kstrtoint(buf, 10, &d);
	if (err)
		return err;

	aiptek->newSetting.programmableDelay = d;
	return count;
}

static DEVICE_ATTR(delay,
		   S_IRUGO | S_IWUSR,
		   show_tabletProgrammableDelay, store_tabletProgrammableDelay);

/***********************************************************************
 * support routines for the 'event_count' file. Note that this file
 * only displays current setting.
 */
static ssize_t show_tabletEventsReceived(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%ld\n", aiptek->eventCount);
}

static DEVICE_ATTR(event_count, S_IRUGO, show_tabletEventsReceived, NULL);

/***********************************************************************
 * support routines for the 'diagnostic' file. Note that this file
 * only displays current setting.
 */
static ssize_t show_tabletDiagnosticMessage(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	char *retMsg;

	switch (aiptek->diagnostic) {
	case AIPTEK_DIAGNOSTIC_NA:
		retMsg = "no errors\n";
		break;

	case AIPTEK_DIAGNOSTIC_SENDING_RELATIVE_IN_ABSOLUTE:
		retMsg = "Error: receiving relative reports\n";
		break;

	case AIPTEK_DIAGNOSTIC_SENDING_ABSOLUTE_IN_RELATIVE:
		retMsg = "Error: receiving absolute reports\n";
		break;

	case AIPTEK_DIAGNOSTIC_TOOL_DISALLOWED:
		if (aiptek->curSetting.pointerMode ==
		    AIPTEK_POINTER_ONLY_MOUSE_MODE) {
			retMsg = "Error: receiving stylus reports\n";
		} else {
			retMsg = "Error: receiving mouse reports\n";
		}
		break;

	default:
		return 0;
	}
	return sysfs_emit(buf, retMsg);
}

static DEVICE_ATTR(diagnostic, S_IRUGO, show_tabletDiagnosticMessage, NULL);

/***********************************************************************
 * support routines for the 'stylus_upper' file. Note that this file
 * both displays current setting and allows for setting changing.
 */

static struct aiptek_map stylus_button_map[] = {
	{ "upper",	AIPTEK_STYLUS_UPPER_BUTTON },
	{ "lower",	AIPTEK_STYLUS_LOWER_BUTTON },
	{ NULL,		AIPTEK_INVALID_VALUE }
};

static ssize_t show_tabletStylusUpper(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(stylus_button_map,
						      aiptek->curSetting.stylusButtonUpper));
}

static ssize_t
store_tabletStylusUpper(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_button = map_str_to_val(stylus_button_map, buf, count);

	if (new_button == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.stylusButtonUpper = new_button;
	return count;
}

static DEVICE_ATTR(stylus_upper,
		   S_IRUGO | S_IWUSR,
		   show_tabletStylusUpper, store_tabletStylusUpper);

/***********************************************************************
 * support routines for the 'stylus_lower' file. Note that this file
 * both displays current setting and allows for setting changing.
 */

static ssize_t show_tabletStylusLower(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(stylus_button_map,
						      aiptek->curSetting.stylusButtonLower));
}

static ssize_t
store_tabletStylusLower(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_button = map_str_to_val(stylus_button_map, buf, count);

	if (new_button == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.stylusButtonLower = new_button;
	return count;
}

static DEVICE_ATTR(stylus_lower,
		   S_IRUGO | S_IWUSR,
		   show_tabletStylusLower, store_tabletStylusLower);

/***********************************************************************
 * support routines for the 'mouse_left' file. Note that this file
 * both displays current setting and allows for setting changing.
 */

static struct aiptek_map mouse_button_map[] = {
	{ "left",	AIPTEK_MOUSE_LEFT_BUTTON },
	{ "middle",	AIPTEK_MOUSE_MIDDLE_BUTTON },
	{ "right",	AIPTEK_MOUSE_RIGHT_BUTTON },
	{ NULL,		AIPTEK_INVALID_VALUE }
};

static ssize_t show_tabletMouseLeft(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(mouse_button_map,
						      aiptek->curSetting.mouseButtonLeft));
}

static ssize_t
store_tabletMouseLeft(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_button = map_str_to_val(mouse_button_map, buf, count);

	if (new_button == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.mouseButtonLeft = new_button;
	return count;
}

static DEVICE_ATTR(mouse_left,
		   S_IRUGO | S_IWUSR,
		   show_tabletMouseLeft, store_tabletMouseLeft);

/***********************************************************************
 * support routines for the 'mouse_middle' file. Note that this file
 * both displays current setting and allows for setting changing.
 */
static ssize_t show_tabletMouseMiddle(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(mouse_button_map,
						      aiptek->curSetting.mouseButtonMiddle));
}

static ssize_t
store_tabletMouseMiddle(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_button = map_str_to_val(mouse_button_map, buf, count);

	if (new_button == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.mouseButtonMiddle = new_button;
	return count;
}

static DEVICE_ATTR(mouse_middle,
		   S_IRUGO | S_IWUSR,
		   show_tabletMouseMiddle, store_tabletMouseMiddle);

/***********************************************************************
 * support routines for the 'mouse_right' file. Note that this file
 * both displays current setting and allows for setting changing.
 */
static ssize_t show_tabletMouseRight(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", map_val_to_str(mouse_button_map,
						      aiptek->curSetting.mouseButtonRight));
}

static ssize_t
store_tabletMouseRight(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int new_button = map_str_to_val(mouse_button_map, buf, count);

	if (new_button == AIPTEK_INVALID_VALUE)
		return -EINVAL;

	aiptek->newSetting.mouseButtonRight = new_button;
	return count;
}

static DEVICE_ATTR(mouse_right,
		   S_IRUGO | S_IWUSR,
		   show_tabletMouseRight, store_tabletMouseRight);

/***********************************************************************
 * support routines for the 'wheel' file. Note that this file
 * both displays current setting and allows for setting changing.
 */
static ssize_t show_tabletWheel(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	if (aiptek->curSetting.wheel == AIPTEK_WHEEL_DISABLE) {
		return sysfs_emit(buf, "disable\n");
	} else {
		return sysfs_emit(buf, "%d\n", aiptek->curSetting.wheel);
	}
}

static ssize_t
store_tabletWheel(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);
	int err, w;

	err = kstrtoint(buf, 10, &w);
	if (err)
		return err;

	aiptek->newSetting.wheel = w;
	return count;
}

static DEVICE_ATTR(wheel,
		   S_IRUGO | S_IWUSR, show_tabletWheel, store_tabletWheel);

/***********************************************************************
 * support routines for the 'execute' file. Note that this file
 * both displays current setting and allows for setting changing.
 */
static ssize_t show_tabletExecute(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* There is nothing useful to display, so a one-line manual
	 * is in order...
	 */
	return sysfs_emit(buf, "Write anything to this file to program your tablet.\n");
}

static ssize_t
store_tabletExecute(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	/* We do not care what you write to this file. Merely the action
	 * of writing to this file triggers a tablet reprogramming.
	 */
	memcpy(&aiptek->curSetting, &aiptek->newSetting,
	       sizeof(struct aiptek_settings));

	if (aiptek_program_tablet(aiptek) < 0)
		return -EIO;

	return count;
}

static DEVICE_ATTR(execute,
		   S_IRUGO | S_IWUSR, show_tabletExecute, store_tabletExecute);

/***********************************************************************
 * support routines for the 'odm_code' file. Note that this file
 * only displays current setting.
 */
static ssize_t show_tabletODMCode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%04x\n", aiptek->features.odmCode);
}

static DEVICE_ATTR(odm_code, S_IRUGO, show_tabletODMCode, NULL);

/***********************************************************************
 * support routines for the 'model_code' file. Note that this file
 * only displays current setting.
 */
static ssize_t show_tabletModelCode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%04x\n", aiptek->features.modelCode);
}

static DEVICE_ATTR(model_code, S_IRUGO, show_tabletModelCode, NULL);

/***********************************************************************
 * support routines for the 'firmware_code' file. Note that this file
 * only displays current setting.
 */
static ssize_t show_firmwareCode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aiptek *aiptek = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%04x\n", aiptek->features.firmwareCode);
}

static DEVICE_ATTR(firmware_code, S_IRUGO, show_firmwareCode, NULL);

static struct attribute *aiptek_attributes[] = {
	&dev_attr_size.attr,
	&dev_attr_pointer_mode.attr,
	&dev_attr_coordinate_mode.attr,
	&dev_attr_tool_mode.attr,
	&dev_attr_xtilt.attr,
	&dev_attr_ytilt.attr,
	&dev_attr_jitter.attr,
	&dev_attr_delay.attr,
	&dev_attr_event_count.attr,
	&dev_attr_diagnostic.attr,
	&dev_attr_odm_code.attr,
	&dev_attr_model_code.attr,
	&dev_attr_firmware_code.attr,
	&dev_attr_stylus_lower.attr,
	&dev_attr_stylus_upper.attr,
	&dev_attr_mouse_left.attr,
	&dev_attr_mouse_middle.attr,
	&dev_attr_mouse_right.attr,
	&dev_attr_wheel.attr,
	&dev_attr_execute.attr,
	NULL
};

static const struct attribute_group aiptek_attribute_group = {
	.attrs	= aiptek_attributes,
};

/***********************************************************************
 * This routine is called when a tablet has been identified. It basically
 * sets up the tablet and the driver's internal structures.
 */
static int
aiptek_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct aiptek *aiptek;
	struct input_dev *inputdev;
	int i;
	int speeds[] = { 0,
		AIPTEK_PROGRAMMABLE_DELAY_50,
		AIPTEK_PROGRAMMABLE_DELAY_400,
		AIPTEK_PROGRAMMABLE_DELAY_25,
		AIPTEK_PROGRAMMABLE_DELAY_100,
		AIPTEK_PROGRAMMABLE_DELAY_200,
		AIPTEK_PROGRAMMABLE_DELAY_300
	};
	int err = -ENOMEM;

	/* programmableDelay is where the command-line specified
	 * delay is kept. We make it the first element of speeds[],
	 * so therefore, your override speed is tried first, then the
	 * remainder. Note that the default value of 400ms will be tried
	 * if you do not specify any command line parameter.
	 */
	speeds[0] = programmableDelay;

	aiptek = kzalloc(sizeof(struct aiptek), GFP_KERNEL);
	inputdev = input_allocate_device();
	if (!aiptek || !inputdev) {
		dev_warn(&intf->dev,
			 "cannot allocate memory or input device\n");
		goto fail1;
        }

	aiptek->data = usb_alloc_coherent(usbdev, AIPTEK_PACKET_LENGTH,
					  GFP_KERNEL, &aiptek->data_dma);
        if (!aiptek->data) {
		dev_warn(&intf->dev, "cannot allocate usb buffer\n");
		goto fail1;
	}

	aiptek->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!aiptek->urb) {
	        dev_warn(&intf->dev, "cannot allocate urb\n");
		goto fail2;
	}

	aiptek->inputdev = inputdev;
	aiptek->intf = intf;
	aiptek->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	aiptek->inDelay = 0;
	aiptek->endDelay = 0;
	aiptek->previousJitterable = 0;
	aiptek->lastMacro = -1;

	/* Set up the curSettings struct. Said struct contains the current
	 * programmable parameters. The newSetting struct contains changes
	 * the user makes to the settings via the sysfs interface. Those
	 * changes are not "committed" to curSettings until the user
	 * writes to the sysfs/.../execute file.
	 */
	aiptek->curSetting.pointerMode = AIPTEK_POINTER_EITHER_MODE;
	aiptek->curSetting.coordinateMode = AIPTEK_COORDINATE_ABSOLUTE_MODE;
	aiptek->curSetting.toolMode = AIPTEK_TOOL_BUTTON_PEN_MODE;
	aiptek->curSetting.xTilt = AIPTEK_TILT_DISABLE;
	aiptek->curSetting.yTilt = AIPTEK_TILT_DISABLE;
	aiptek->curSetting.mouseButtonLeft = AIPTEK_MOUSE_LEFT_BUTTON;
	aiptek->curSetting.mouseButtonMiddle = AIPTEK_MOUSE_MIDDLE_BUTTON;
	aiptek->curSetting.mouseButtonRight = AIPTEK_MOUSE_RIGHT_BUTTON;
	aiptek->curSetting.stylusButtonUpper = AIPTEK_STYLUS_UPPER_BUTTON;
	aiptek->curSetting.stylusButtonLower = AIPTEK_STYLUS_LOWER_BUTTON;
	aiptek->curSetting.jitterDelay = jitterDelay;
	aiptek->curSetting.programmableDelay = programmableDelay;

	/* Both structs should have equivalent settings
	 */
	aiptek->newSetting = aiptek->curSetting;

	/* Determine the usb devices' physical path.
	 * Asketh not why we always pretend we're using "../input0",
	 * but I suspect this will have to be refactored one
	 * day if a single USB device can be a keyboard & a mouse
	 * & a tablet, and the inputX number actually will tell
	 * us something...
	 */
	usb_make_path(usbdev, aiptek->features.usbPath,
			sizeof(aiptek->features.usbPath));
	strlcat(aiptek->features.usbPath, "/input0",
		sizeof(aiptek->features.usbPath));

	/* Set up client data, pointers to open and close routines
	 * for the input device.
	 */
	inputdev->name = "Aiptek";
	inputdev->phys = aiptek->features.usbPath;
	usb_to_input_id(usbdev, &inputdev->id);
	inputdev->dev.parent = &intf->dev;

	input_set_drvdata(inputdev, aiptek);

	inputdev->open = aiptek_open;
	inputdev->close = aiptek_close;

	/* Now program the capacities of the tablet, in terms of being
	 * an input device.
	 */
	for (i = 0; i < ARRAY_SIZE(eventTypes); ++i)
	        __set_bit(eventTypes[i], inputdev->evbit);

	for (i = 0; i < ARRAY_SIZE(absEvents); ++i)
	        __set_bit(absEvents[i], inputdev->absbit);

	for (i = 0; i < ARRAY_SIZE(relEvents); ++i)
	        __set_bit(relEvents[i], inputdev->relbit);

	__set_bit(MSC_SERIAL, inputdev->mscbit);

	/* Set up key and button codes */
	for (i = 0; i < ARRAY_SIZE(buttonEvents); ++i)
		__set_bit(buttonEvents[i], inputdev->keybit);

	for (i = 0; i < ARRAY_SIZE(macroKeyEvents); ++i)
		__set_bit(macroKeyEvents[i], inputdev->keybit);

	/*
	 * Program the input device coordinate capacities. We do not yet
	 * know what maximum X, Y, and Z values are, so we're putting fake
	 * values in. Later, we'll ask the tablet to put in the correct
	 * values.
	 */
	input_set_abs_params(inputdev, ABS_X, 0, 2999, 0, 0);
	input_set_abs_params(inputdev, ABS_Y, 0, 2249, 0, 0);
	input_set_abs_params(inputdev, ABS_PRESSURE, 0, 511, 0, 0);
	input_set_abs_params(inputdev, ABS_TILT_X, AIPTEK_TILT_MIN, AIPTEK_TILT_MAX, 0, 0);
	input_set_abs_params(inputdev, ABS_TILT_Y, AIPTEK_TILT_MIN, AIPTEK_TILT_MAX, 0, 0);
	input_set_abs_params(inputdev, ABS_WHEEL, AIPTEK_WHEEL_MIN, AIPTEK_WHEEL_MAX - 1, 0, 0);

	err = usb_find_common_endpoints(intf->cur_altsetting,
					NULL, NULL, &endpoint, NULL);
	if (err) {
		dev_err(&intf->dev,
			"interface has no int in endpoints, but must have minimum 1\n");
		goto fail3;
	}

	/* Go set up our URB, which is called when the tablet receives
	 * input.
	 */
	usb_fill_int_urb(aiptek->urb,
			 usbdev,
			 usb_rcvintpipe(usbdev,
					endpoint->bEndpointAddress),
			 aiptek->data, 8, aiptek_irq, aiptek,
			 endpoint->bInterval);

	aiptek->urb->transfer_dma = aiptek->data_dma;
	aiptek->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* Program the tablet. This sets the tablet up in the mode
	 * specified in newSetting, and also queries the tablet's
	 * physical capacities.
	 *
	 * Sanity check: if a tablet doesn't like the slow programmatic
	 * delay, we often get sizes of 0x0. Let's use that as an indicator
	 * to try faster delays, up to 25 ms. If that logic fails, well, you'll
	 * have to explain to us how your tablet thinks it's 0x0, and yet that's
	 * not an error :-)
	 */

	for (i = 0; i < ARRAY_SIZE(speeds); ++i) {
		aiptek->curSetting.programmableDelay = speeds[i];
		(void)aiptek_program_tablet(aiptek);
		if (input_abs_get_max(aiptek->inputdev, ABS_X) > 0) {
			dev_info(&intf->dev,
				 "Aiptek using %d ms programming speed\n",
				 aiptek->curSetting.programmableDelay);
			break;
		}
	}

	/* Murphy says that some day someone will have a tablet that fails the
	   above test. That's you, Frederic Rodrigo */
	if (i == ARRAY_SIZE(speeds)) {
		dev_info(&intf->dev,
			 "Aiptek tried all speeds, no sane response\n");
		err = -EINVAL;
		goto fail3;
	}

	/* Associate this driver's struct with the usb interface.
	 */
	usb_set_intfdata(intf, aiptek);

	/* Set up the sysfs files
	 */
	err = sysfs_create_group(&intf->dev.kobj, &aiptek_attribute_group);
	if (err) {
		dev_warn(&intf->dev, "cannot create sysfs group err: %d\n",
			 err);
		goto fail3;
        }

	/* Register the tablet as an Input Device
	 */
	err = input_register_device(aiptek->inputdev);
	if (err) {
		dev_warn(&intf->dev,
			 "input_register_device returned err: %d\n", err);
		goto fail4;
        }
	return 0;

 fail4:	sysfs_remove_group(&intf->dev.kobj, &aiptek_attribute_group);
 fail3: usb_free_urb(aiptek->urb);
 fail2:	usb_free_coherent(usbdev, AIPTEK_PACKET_LENGTH, aiptek->data,
			  aiptek->data_dma);
 fail1: usb_set_intfdata(intf, NULL);
	input_free_device(inputdev);
	kfree(aiptek);
	return err;
}

/***********************************************************************
 * Deal with tablet disconnecting from the system.
 */
static void aiptek_disconnect(struct usb_interface *intf)
{
	struct aiptek *aiptek = usb_get_intfdata(intf);

	/* Disassociate driver's struct with usb interface
	 */
	usb_set_intfdata(intf, NULL);
	if (aiptek != NULL) {
		/* Free & unhook everything from the system.
		 */
		usb_kill_urb(aiptek->urb);
		input_unregister_device(aiptek->inputdev);
		sysfs_remove_group(&intf->dev.kobj, &aiptek_attribute_group);
		usb_free_urb(aiptek->urb);
		usb_free_coherent(interface_to_usbdev(intf),
				  AIPTEK_PACKET_LENGTH,
				  aiptek->data, aiptek->data_dma);
		kfree(aiptek);
	}
}

static struct usb_driver aiptek_driver = {
	.name = "aiptek",
	.probe = aiptek_probe,
	.disconnect = aiptek_disconnect,
	.id_table = aiptek_ids,
};

module_usb_driver(aiptek_driver);

MODULE_AUTHOR("Bryan W. Headley/Chris Atenasio/Cedric Brun/Rene van Paassen");
MODULE_DESCRIPTION("Aiptek HyperPen USB Tablet Driver");
MODULE_LICENSE("GPL");

module_param(programmableDelay, int, 0);
MODULE_PARM_DESC(programmableDelay, "delay used during tablet programming");
module_param(jitterDelay, int, 0);
MODULE_PARM_DESC(jitterDelay, "stylus/mouse settlement delay");
