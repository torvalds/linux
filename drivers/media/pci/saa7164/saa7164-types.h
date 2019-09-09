/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 */

/* TODO: Cleanup and shorten the namespace */

/* Some structues are passed directly to/from the firmware and
 * have strict alignment requirements. This is one of them.
 */
struct tmComResHWDescr {
	u8	bLength;
	u8	bDescriptorType;
	u8	bDescriptorSubtype;
	u16	bcdSpecVersion;
	u32	dwClockFrequency;
	u32	dwClockUpdateRes;
	u8	bCapabilities;
	u32	dwDeviceRegistersLocation;
	u32	dwHostMemoryRegion;
	u32	dwHostMemoryRegionSize;
	u32	dwHostHibernatMemRegion;
	u32	dwHostHibernatMemRegionSize;
} __attribute__((packed));

/* This is DWORD aligned on windows but I can't find the right
 * gcc syntax to match the binary data from the device.
 * I've manually padded with Reserved[3] bytes to match the hardware,
 * but this could break if GCC decies to pack in a different way.
 */
struct tmComResInterfaceDescr {
	u8	bLength;
	u8	bDescriptorType;
	u8	bDescriptorSubtype;
	u8	bFlags;
	u8	bInterfaceType;
	u8	bInterfaceId;
	u8	bBaseInterface;
	u8	bInterruptId;
	u8	bDebugInterruptId;
	u8	BARLocation;
	u8	Reserved[3];
};

struct tmComResBusDescr {
	u64	CommandRing;
	u64	ResponseRing;
	u32	CommandWrite;
	u32	CommandRead;
	u32	ResponseWrite;
	u32	ResponseRead;
};

enum tmBusType {
	NONE		= 0,
	TYPE_BUS_PCI	= 1,
	TYPE_BUS_PCIe	= 2,
	TYPE_BUS_USB	= 3,
	TYPE_BUS_I2C	= 4
};

struct tmComResBusInfo {
	enum tmBusType Type;
	u16	m_wMaxReqSize;
	u8 __iomem *m_pdwSetRing;
	u32	m_dwSizeSetRing;
	u8 __iomem *m_pdwGetRing;
	u32	m_dwSizeGetRing;
	u32	m_dwSetWritePos;
	u32	m_dwSetReadPos;
	u32	m_dwGetWritePos;
	u32	m_dwGetReadPos;

	/* All access is protected */
	struct mutex lock;

};

struct tmComResInfo {
	u8	id;
	u8	flags;
	u16	size;
	u32	command;
	u16	controlselector;
	u8	seqno;
} __attribute__((packed));

enum tmComResCmd {
	SET_CUR  = 0x01,
	GET_CUR  = 0x81,
	GET_MIN  = 0x82,
	GET_MAX  = 0x83,
	GET_RES  = 0x84,
	GET_LEN  = 0x85,
	GET_INFO = 0x86,
	GET_DEF  = 0x87
};

struct cmd {
	u8 seqno;
	u32 inuse;
	u32 timeout;
	u32 signalled;
	struct mutex lock;
	wait_queue_head_t wait;
};

struct tmDescriptor {
	u32	pathid;
	u32	size;
	void	*descriptor;
};

struct tmComResDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
} __attribute__((packed));

struct tmComResExtDevDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u32	devicetype;
	u16	deviceid;
	u32	numgpiopins;
	u8	numgpiogroups;
	u8	controlsize;
} __attribute__((packed));

struct tmComResGPIO {
	u32	pin;
	u8	state;
} __attribute__((packed));

struct tmComResPathDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	pathid;
} __attribute__((packed));

/* terminaltype */
enum tmComResTermType {
	ITT_ANTENNA              = 0x0203,
	LINE_CONNECTOR           = 0x0603,
	SPDIF_CONNECTOR          = 0x0605,
	COMPOSITE_CONNECTOR      = 0x0401,
	SVIDEO_CONNECTOR         = 0x0402,
	COMPONENT_CONNECTOR      = 0x0403,
	STANDARD_DMA             = 0xF101
};

struct tmComResAntTermDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	terminalid;
	u16	terminaltype;
	u8	assocterminal;
	u8	iterminal;
	u8	controlsize;
} __attribute__((packed));

struct tmComResTunerDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u8	sourceid;
	u8	iunit;
	u32	tuningstandards;
	u8	controlsize;
	u32	controls;
} __attribute__((packed));

enum tmBufferFlag {
	/* the buffer does not contain any valid data */
	TM_BUFFER_FLAG_EMPTY,

	/* the buffer is filled with valid data */
	TM_BUFFER_FLAG_DONE,

	/* the buffer is the dummy buffer - TODO??? */
	TM_BUFFER_FLAG_DUMMY_BUFFER
};

struct tmBuffer {
	u64		*pagetablevirt;
	u64		pagetablephys;
	u16		offset;
	u8		*context;
	u64		timestamp;
	enum tmBufferFlag BufferFlag;
	u32		lostbuffers;
	u32		validbuffers;
	u64		*dummypagevirt;
	u64		dummypagephys;
	u64		*addressvirt;
};

struct tmHWStreamParameters {
	u32	bitspersample;
	u32	samplesperline;
	u32	numberoflines;
	u32	pitch;
	u32	linethreshold;
	u64	**pagetablelistvirt;
	u64	*pagetablelistphys;
	u32	numpagetables;
	u32	numpagetableentries;
};

struct tmStreamParameters {
	struct tmHWStreamParameters	HWStreamParameters;
	u64				qwDummyPageTablePhys;
	u64				*pDummyPageTableVirt;
};

struct tmComResDMATermDescrHeader {
	u8	len;
	u8	type;
	u8	subtyle;
	u8	unitid;
	u16	terminaltype;
	u8	assocterminal;
	u8	sourceid;
	u8	iterminal;
	u32	BARLocation;
	u8	flags;
	u8	interruptid;
	u8	buffercount;
	u8	metadatasize;
	u8	numformats;
	u8	controlsize;
} __attribute__((packed));

/*
 *
 * Description:
 *  This is the transport stream format header.
 *
 * Settings:
 *  bLength                 - The size of this descriptor in bytes.
 *  bDescriptorType         - CS_INTERFACE.
 *  bDescriptorSubtype      - VS_FORMAT_MPEG2TS descriptor subtype.
 *  bFormatIndex            - A non-zero constant that uniquely identifies the
 *                            format.
 *  bDataOffset             - Offset to TSP packet within MPEG-2 TS transport
 *                            stride, in bytes.
 *  bPacketLength           - Length of TSP packet, in bytes (typically 188).
 *  bStrideLength           - Length of MPEG-2 TS transport stride.
 *  guidStrideFormat        - A Globally Unique Identifier indicating the
 *                            format of the stride data (if any). Set to zeros
 *                            if there is no Stride Data, or if the Stride
 *                            Data is to be ignored by the application.
 *
 */
struct tmComResTSFormatDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	bFormatIndex;
	u8	bDataOffset;
	u8	bPacketLength;
	u8	bStrideLength;
	u8	guidStrideFormat[16];
} __attribute__((packed));

/* Encoder related structures */

/* A/V Mux Selector */
struct tmComResSelDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u8	nrinpins;
	u8	sourceid;
} __attribute__((packed));

/* A/V Audio processor definitions */
struct tmComResProcDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u8	sourceid;
	u16	wreserved;
	u8	controlsize;
} __attribute__((packed));

/* Video bitrate control message */
#define EU_VIDEO_BIT_RATE_MODE_CONSTANT		(0)
#define EU_VIDEO_BIT_RATE_MODE_VARIABLE_AVERAGE (1)
#define EU_VIDEO_BIT_RATE_MODE_VARIABLE_PEAK	(2)
struct tmComResEncVideoBitRate {
	u8	ucVideoBitRateMode;
	u32	dwVideoBitRate;
	u32	dwVideoBitRatePeak;
} __attribute__((packed));

/* Video Encoder Aspect Ratio message */
struct tmComResEncVideoInputAspectRatio {
	u8	width;
	u8	height;
} __attribute__((packed));

/* Video Encoder GOP IBP message */
/* 1. IPPPPPPPPPPPPPP */
/* 2. IBPBPBPBPBPBPBP */
/* 3. IBBPBBPBBPBBP   */
#define SAA7164_ENCODER_DEFAULT_GOP_DIST (1)
#define SAA7164_ENCODER_DEFAULT_GOP_SIZE (15)
struct tmComResEncVideoGopStructure {
	u8	ucGOPSize;	/* GOP Size 12, 15 */
	u8	ucRefFrameDist; /* Reference Frame Distance */
} __attribute__((packed));

/* Encoder processor definition */
struct tmComResEncoderDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u8	vsourceid;
	u8	asourceid;
	u8	iunit;
	u32	dwmControlCap;
	u32	dwmProfileCap;
	u32	dwmVidFormatCap;
	u8	bmVidBitrateCap;
	u16	wmVidResolutionsCap;
	u16	wmVidFrmRateCap;
	u32	dwmAudFormatCap;
	u8	bmAudBitrateCap;
} __attribute__((packed));

/* Audio processor definition */
struct tmComResAFeatureDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	unitid;
	u8	sourceid;
	u8	controlsize;
} __attribute__((packed));

/* Audio control messages */
struct tmComResAudioDefaults {
	u8	ucDecoderLevel;
	u8	ucDecoderFM_Level;
	u8	ucMonoLevel;
	u8	ucNICAM_Level;
	u8	ucSAP_Level;
	u8	ucADC_Level;
} __attribute__((packed));

/* Audio bitrate control message */
struct tmComResEncAudioBitRate {
	u8	ucAudioBitRateMode;
	u32	dwAudioBitRate;
	u32	dwAudioBitRatePeak;
} __attribute__((packed));

/* Tuner / AV Decoder messages */
struct tmComResTunerStandard {
	u8	std;
	u32	country;
} __attribute__((packed));

struct tmComResTunerStandardAuto {
	u8	mode;
} __attribute__((packed));

/* EEPROM definition for PS stream types */
struct tmComResPSFormatDescrHeader {
	u8	len;
	u8	type;
	u8	subtype;
	u8	bFormatIndex;
	u16	wPacketLength;
	u16	wPackLength;
	u8	bPackDataType;
} __attribute__((packed));

/* VBI control structure */
struct tmComResVBIFormatDescrHeader {
	u8	len;
	u8	type;
	u8	subtype; /* VS_FORMAT_VBI */
	u8	bFormatIndex;
	u32	VideoStandard; /* See KS_AnalogVideoStandard, NTSC = 1 */
	u8	StartLine; /* NTSC Start = 10 */
	u8	EndLine; /* NTSC = 21 */
	u8	FieldRate; /* 60 for NTSC */
	u8	bNumLines; /* Unused - scheduled for removal */
} __attribute__((packed));

struct tmComResProbeCommit {
	u16	bmHint;
	u8	bFormatIndex;
	u8	bFrameIndex;
} __attribute__((packed));

struct tmComResDebugSetLevel {
	u32	dwDebugLevel;
} __attribute__((packed));

struct tmComResDebugGetData {
	u32	dwResult;
	u8	ucDebugData[256];
} __attribute__((packed));

struct tmFwInfoStruct {
	u32	status;
	u32	mode;
	u32	devicespec;
	u32	deviceinst;
	u32	CPULoad;
	u32	RemainHeap;
	u32	CPUClock;
	u32	RAMSpeed;
} __attribute__((packed));
