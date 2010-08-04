/*
 *	f_uvc.h  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#ifndef _F_UVC_H_
#define _F_UVC_H_

#include <linux/usb/composite.h>

#define USB_CLASS_VIDEO_CONTROL		1
#define USB_CLASS_VIDEO_STREAMING	2

struct uvc_descriptor_header {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
} __attribute__ ((packed));

struct uvc_header_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u16 bcdUVC;
	__u16 wTotalLength;
	__u32 dwClockFrequency;
	__u8  bInCollection;
	__u8  baInterfaceNr[];
} __attribute__((__packed__));

#define UVC_HEADER_DESCRIPTOR(n)	uvc_header_descriptor_##n

#define DECLARE_UVC_HEADER_DESCRIPTOR(n) 			\
struct UVC_HEADER_DESCRIPTOR(n) {				\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u16 bcdUVC;						\
	__u16 wTotalLength;					\
	__u32 dwClockFrequency;					\
	__u8  bInCollection;					\
	__u8  baInterfaceNr[n];					\
} __attribute__ ((packed))

struct uvc_input_terminal_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bTerminalID;
	__u16 wTerminalType;
	__u8  bAssocTerminal;
	__u8  iTerminal;
} __attribute__((__packed__));

struct uvc_output_terminal_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bTerminalID;
	__u16 wTerminalType;
	__u8  bAssocTerminal;
	__u8  bSourceID;
	__u8  iTerminal;
} __attribute__((__packed__));

struct uvc_camera_terminal_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bTerminalID;
	__u16 wTerminalType;
	__u8  bAssocTerminal;
	__u8  iTerminal;
	__u16 wObjectiveFocalLengthMin;
	__u16 wObjectiveFocalLengthMax;
	__u16 wOcularFocalLength;
	__u8  bControlSize;
	__u8  bmControls[3];
} __attribute__((__packed__));

struct uvc_selector_unit_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bUnitID;
	__u8  bNrInPins;
	__u8  baSourceID[0];
	__u8  iSelector;
} __attribute__((__packed__));

#define UVC_SELECTOR_UNIT_DESCRIPTOR(n)	\
	uvc_selector_unit_descriptor_##n

#define DECLARE_UVC_SELECTOR_UNIT_DESCRIPTOR(n) 		\
struct UVC_SELECTOR_UNIT_DESCRIPTOR(n) {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bUnitID;						\
	__u8  bNrInPins;					\
	__u8  baSourceID[n];					\
	__u8  iSelector;					\
} __attribute__ ((packed))

struct uvc_processing_unit_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bUnitID;
	__u8  bSourceID;
	__u16 wMaxMultiplier;
	__u8  bControlSize;
	__u8  bmControls[2];
	__u8  iProcessing;
} __attribute__((__packed__));

struct uvc_extension_unit_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bUnitID;
	__u8  guidExtensionCode[16];
	__u8  bNumControls;
	__u8  bNrInPins;
	__u8  baSourceID[0];
	__u8  bControlSize;
	__u8  bmControls[0];
	__u8  iExtension;
} __attribute__((__packed__));

#define UVC_EXTENSION_UNIT_DESCRIPTOR(p, n) \
	uvc_extension_unit_descriptor_##p_##n

#define DECLARE_UVC_EXTENSION_UNIT_DESCRIPTOR(p, n) 		\
struct UVC_EXTENSION_UNIT_DESCRIPTOR(p, n) {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bUnitID;						\
	__u8  guidExtensionCode[16];				\
	__u8  bNumControls;					\
	__u8  bNrInPins;					\
	__u8  baSourceID[p];					\
	__u8  bControlSize;					\
	__u8  bmControls[n];					\
	__u8  iExtension;					\
} __attribute__ ((packed))

struct uvc_control_endpoint_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u16 wMaxTransferSize;
} __attribute__((__packed__));

#define UVC_DT_HEADER				1
#define UVC_DT_INPUT_TERMINAL			2
#define UVC_DT_OUTPUT_TERMINAL			3
#define UVC_DT_SELECTOR_UNIT			4
#define UVC_DT_PROCESSING_UNIT			5
#define UVC_DT_EXTENSION_UNIT			6

#define UVC_DT_HEADER_SIZE(n)			(12+(n))
#define UVC_DT_INPUT_TERMINAL_SIZE		8
#define UVC_DT_OUTPUT_TERMINAL_SIZE		9
#define UVC_DT_CAMERA_TERMINAL_SIZE(n)		(15+(n))
#define UVC_DT_SELECTOR_UNIT_SIZE(n)		(6+(n))
#define UVC_DT_PROCESSING_UNIT_SIZE(n)		(9+(n))
#define UVC_DT_EXTENSION_UNIT_SIZE(p,n)		(24+(p)+(n))
#define UVC_DT_CONTROL_ENDPOINT_SIZE		5

struct uvc_input_header_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bNumFormats;
	__u16 wTotalLength;
	__u8  bEndpointAddress;
	__u8  bmInfo;
	__u8  bTerminalLink;
	__u8  bStillCaptureMethod;
	__u8  bTriggerSupport;
	__u8  bTriggerUsage;
	__u8  bControlSize;
	__u8  bmaControls[];
} __attribute__((__packed__));

#define UVC_INPUT_HEADER_DESCRIPTOR(n, p) \
	uvc_input_header_descriptor_##n_##p

#define DECLARE_UVC_INPUT_HEADER_DESCRIPTOR(n, p)		\
struct UVC_INPUT_HEADER_DESCRIPTOR(n, p) {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bNumFormats;					\
	__u16 wTotalLength;					\
	__u8  bEndpointAddress;					\
	__u8  bmInfo;						\
	__u8  bTerminalLink;					\
	__u8  bStillCaptureMethod;				\
	__u8  bTriggerSupport;					\
	__u8  bTriggerUsage;					\
	__u8  bControlSize;					\
	__u8  bmaControls[p][n];				\
} __attribute__ ((packed))

struct uvc_output_header_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bNumFormats;
	__u16 wTotalLength;
	__u8  bEndpointAddress;
	__u8  bTerminalLink;
	__u8  bControlSize;
	__u8  bmaControls[];
} __attribute__((__packed__));

#define UVC_OUTPUT_HEADER_DESCRIPTOR(n, p) \
	uvc_output_header_descriptor_##n_##p

#define DECLARE_UVC_OUTPUT_HEADER_DESCRIPTOR(n, p)		\
struct UVC_OUTPUT_HEADER_DESCRIPTOR(n, p) {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bNumFormats;					\
	__u16 wTotalLength;					\
	__u8  bEndpointAddress;					\
	__u8  bTerminalLink;					\
	__u8  bControlSize;					\
	__u8  bmaControls[p][n];				\
} __attribute__ ((packed))

struct uvc_format_uncompressed {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bFormatIndex;
	__u8  bNumFrameDescriptors;
	__u8  guidFormat[16];
	__u8  bBitsPerPixel;
	__u8  bDefaultFrameIndex;
	__u8  bAspectRatioX;
	__u8  bAspectRatioY;
	__u8  bmInterfaceFlags;
	__u8  bCopyProtect;
} __attribute__((__packed__));

struct uvc_frame_uncompressed {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bFrameIndex;
	__u8  bmCapabilities;
	__u16 wWidth;
	__u16 wHeight;
	__u32 dwMinBitRate;
	__u32 dwMaxBitRate;
	__u32 dwMaxVideoFrameBufferSize;
	__u32 dwDefaultFrameInterval;
	__u8  bFrameIntervalType;
	__u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_FRAME_UNCOMPRESSED(n) \
	uvc_frame_uncompressed_##n

#define DECLARE_UVC_FRAME_UNCOMPRESSED(n) 			\
struct UVC_FRAME_UNCOMPRESSED(n) {				\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bFrameIndex;					\
	__u8  bmCapabilities;					\
	__u16 wWidth;						\
	__u16 wHeight;						\
	__u32 dwMinBitRate;					\
	__u32 dwMaxBitRate;					\
	__u32 dwMaxVideoFrameBufferSize;			\
	__u32 dwDefaultFrameInterval;				\
	__u8  bFrameIntervalType;				\
	__u32 dwFrameInterval[n];				\
} __attribute__ ((packed))

struct uvc_format_mjpeg {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bFormatIndex;
	__u8  bNumFrameDescriptors;
	__u8  bmFlags;
	__u8  bDefaultFrameIndex;
	__u8  bAspectRatioX;
	__u8  bAspectRatioY;
	__u8  bmInterfaceFlags;
	__u8  bCopyProtect;
} __attribute__((__packed__));

struct uvc_frame_mjpeg {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bFrameIndex;
	__u8  bmCapabilities;
	__u16 wWidth;
	__u16 wHeight;
	__u32 dwMinBitRate;
	__u32 dwMaxBitRate;
	__u32 dwMaxVideoFrameBufferSize;
	__u32 dwDefaultFrameInterval;
	__u8  bFrameIntervalType;
	__u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_FRAME_MJPEG(n) \
	uvc_frame_mjpeg_##n

#define DECLARE_UVC_FRAME_MJPEG(n) 				\
struct UVC_FRAME_MJPEG(n) {					\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubType;				\
	__u8  bFrameIndex;					\
	__u8  bmCapabilities;					\
	__u16 wWidth;						\
	__u16 wHeight;						\
	__u32 dwMinBitRate;					\
	__u32 dwMaxBitRate;					\
	__u32 dwMaxVideoFrameBufferSize;			\
	__u32 dwDefaultFrameInterval;				\
	__u8  bFrameIntervalType;				\
	__u32 dwFrameInterval[n];				\
} __attribute__ ((packed))

struct uvc_color_matching_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bColorPrimaries;
	__u8  bTransferCharacteristics;
	__u8  bMatrixCoefficients;
} __attribute__((__packed__));

#define UVC_DT_INPUT_HEADER			1
#define UVC_DT_OUTPUT_HEADER			2
#define UVC_DT_FORMAT_UNCOMPRESSED		4
#define UVC_DT_FRAME_UNCOMPRESSED		5
#define UVC_DT_FORMAT_MJPEG			6
#define UVC_DT_FRAME_MJPEG			7
#define UVC_DT_COLOR_MATCHING			13

#define UVC_DT_INPUT_HEADER_SIZE(n, p)		(13+(n*p))
#define UVC_DT_OUTPUT_HEADER_SIZE(n, p)		(9+(n*p))
#define UVC_DT_FORMAT_UNCOMPRESSED_SIZE		27
#define UVC_DT_FRAME_UNCOMPRESSED_SIZE(n)	(26+4*(n))
#define UVC_DT_FORMAT_MJPEG_SIZE		11
#define UVC_DT_FRAME_MJPEG_SIZE(n)		(26+4*(n))
#define UVC_DT_COLOR_MATCHING_SIZE		6

extern int uvc_bind_config(struct usb_configuration *c,
			   const struct uvc_descriptor_header * const *control,
			   const struct uvc_descriptor_header * const *fs_streaming,
			   const struct uvc_descriptor_header * const *hs_streaming);

#endif /* _F_UVC_H_ */

