/* SPDX-License-Identifier: (GPL-2.0 OR CDDL-1.0) */
/*
 * Virtual Device for Guest <-> VMM/Host communication interface
 *
 * Copyright (C) 2006-2016 Oracle Corporation
 */

#ifndef __VBOX_VMMDEV_H__
#define __VBOX_VMMDEV_H__

#include <asm/bitsperlong.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/vbox_vmmdev_types.h>

/* Port for generic request interface (relative offset). */
#define VMMDEV_PORT_OFF_REQUEST                             0

/** Layout of VMMDEV RAM region that contains information for guest. */
struct vmmdev_memory {
	/** The size of this structure. */
	u32 size;
	/** The structure version. (VMMDEV_MEMORY_VERSION) */
	u32 version;

	union {
		struct {
			/** Flag telling that VMMDev has events pending. */
			u8 have_events;
			/** Explicit padding, MBZ. */
			u8 padding[3];
		} V1_04;

		struct {
			/** Pending events flags, set by host. */
			u32 host_events;
			/** Mask of events the guest wants, set by guest. */
			u32 guest_event_mask;
		} V1_03;
	} V;

	/* struct vbva_memory, not used */
};
VMMDEV_ASSERT_SIZE(vmmdev_memory, 8 + 8);

/** Version of vmmdev_memory structure (vmmdev_memory::version). */
#define VMMDEV_MEMORY_VERSION   (1)

/* Host mouse capabilities has been changed. */
#define VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED             BIT(0)
/* HGCM event. */
#define VMMDEV_EVENT_HGCM                                   BIT(1)
/* A display change request has been issued. */
#define VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST                 BIT(2)
/* Credentials are available for judgement. */
#define VMMDEV_EVENT_JUDGE_CREDENTIALS                      BIT(3)
/* The guest has been restored. */
#define VMMDEV_EVENT_RESTORED                               BIT(4)
/* Seamless mode state changed. */
#define VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST           BIT(5)
/* Memory balloon size changed. */
#define VMMDEV_EVENT_BALLOON_CHANGE_REQUEST                 BIT(6)
/* Statistics interval changed. */
#define VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST     BIT(7)
/* VRDP status changed. */
#define VMMDEV_EVENT_VRDP                                   BIT(8)
/* New mouse position data available. */
#define VMMDEV_EVENT_MOUSE_POSITION_CHANGED                 BIT(9)
/* CPU hotplug event occurred. */
#define VMMDEV_EVENT_CPU_HOTPLUG                            BIT(10)
/* The mask of valid events, for sanity checking. */
#define VMMDEV_EVENT_VALID_EVENT_MASK                       0x000007ffU

/*
 * Additions are allowed to work only if additions_major == vmmdev_current &&
 * additions_minor <= vmmdev_current. Additions version is reported to host
 * (VMMDev) by VMMDEVREQ_REPORT_GUEST_INFO.
 */
#define VMMDEV_VERSION                      0x00010004
#define VMMDEV_VERSION_MAJOR                (VMMDEV_VERSION >> 16)
#define VMMDEV_VERSION_MINOR                (VMMDEV_VERSION & 0xffff)

/* Maximum request packet size. */
#define VMMDEV_MAX_VMMDEVREQ_SIZE           1048576

/* Version of vmmdev_request_header structure. */
#define VMMDEV_REQUEST_HEADER_VERSION       0x10001

/** struct vmmdev_request_header - Generic VMMDev request header. */
struct vmmdev_request_header {
	/** IN: Size of the structure in bytes (including body). */
	u32 size;
	/** IN: Version of the structure.  */
	u32 version;
	/** IN: Type of the request. */
	enum vmmdev_request_type request_type;
	/** OUT: Return code. */
	s32 rc;
	/** Reserved field no.1. MBZ. */
	u32 reserved1;
	/** IN: Requestor information (VMMDEV_REQUESTOR_*) */
	u32 requestor;
};
VMMDEV_ASSERT_SIZE(vmmdev_request_header, 24);

/**
 * struct vmmdev_mouse_status - Mouse status request structure.
 *
 * Used by VMMDEVREQ_GET_MOUSE_STATUS and VMMDEVREQ_SET_MOUSE_STATUS.
 */
struct vmmdev_mouse_status {
	/** header */
	struct vmmdev_request_header header;
	/** Mouse feature mask. See VMMDEV_MOUSE_*. */
	u32 mouse_features;
	/** Mouse x position. */
	s32 pointer_pos_x;
	/** Mouse y position. */
	s32 pointer_pos_y;
};
VMMDEV_ASSERT_SIZE(vmmdev_mouse_status, 24 + 12);

/* The guest can (== wants to) handle absolute coordinates.  */
#define VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE                     BIT(0)
/*
 * The host can (== wants to) send absolute coordinates.
 * (Input not captured.)
 */
#define VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE                    BIT(1)
/*
 * The guest can *NOT* switch to software cursor and therefore depends on the
 * host cursor.
 *
 * When guest additions are installed and the host has promised to display the
 * cursor itself, the guest installs a hardware mouse driver. Don't ask the
 * guest to switch to a software cursor then.
 */
#define VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR                BIT(2)
/* The host does NOT provide support for drawing the cursor itself. */
#define VMMDEV_MOUSE_HOST_CANNOT_HWPOINTER                  BIT(3)
/* The guest can read VMMDev events to find out about pointer movement */
#define VMMDEV_MOUSE_NEW_PROTOCOL                           BIT(4)
/*
 * If the guest changes the status of the VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR
 * bit, the host will honour this.
 */
#define VMMDEV_MOUSE_HOST_RECHECKS_NEEDS_HOST_CURSOR        BIT(5)
/*
 * The host supplies an absolute pointing device.  The Guest Additions may
 * wish to use this to decide whether to install their own driver.
 */
#define VMMDEV_MOUSE_HOST_HAS_ABS_DEV                       BIT(6)

/* The minimum value our pointing device can return. */
#define VMMDEV_MOUSE_RANGE_MIN 0
/* The maximum value our pointing device can return. */
#define VMMDEV_MOUSE_RANGE_MAX 0xFFFF

/**
 * struct vmmdev_host_version - VirtualBox host version request structure.
 *
 * VBG uses this to detect the precense of new features in the interface.
 */
struct vmmdev_host_version {
	/** Header. */
	struct vmmdev_request_header header;
	/** Major version. */
	u16 major;
	/** Minor version. */
	u16 minor;
	/** Build number. */
	u32 build;
	/** SVN revision. */
	u32 revision;
	/** Feature mask. */
	u32 features;
};
VMMDEV_ASSERT_SIZE(vmmdev_host_version, 24 + 16);

/* Physical page lists are supported by HGCM. */
#define VMMDEV_HVF_HGCM_PHYS_PAGE_LIST  BIT(0)

/**
 * struct vmmdev_mask - Structure to set / clear bits in a mask used for
 * VMMDEVREQ_SET_GUEST_CAPABILITIES and VMMDEVREQ_CTL_GUEST_FILTER_MASK.
 */
struct vmmdev_mask {
	/** Header. */
	struct vmmdev_request_header header;
	/** Mask of bits to be set. */
	u32 or_mask;
	/** Mask of bits to be cleared. */
	u32 not_mask;
};
VMMDEV_ASSERT_SIZE(vmmdev_mask, 24 + 8);

/* The guest supports seamless display rendering. */
#define VMMDEV_GUEST_SUPPORTS_SEAMLESS                      BIT(0)
/* The guest supports mapping guest to host windows. */
#define VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING     BIT(1)
/*
 * The guest graphical additions are active.
 * Used for fast activation and deactivation of certain graphical operations
 * (e.g. resizing & seamless). The legacy VMMDEVREQ_REPORT_GUEST_CAPABILITIES
 * request sets this automatically, but VMMDEVREQ_SET_GUEST_CAPABILITIES does
 * not.
 */
#define VMMDEV_GUEST_SUPPORTS_GRAPHICS                      BIT(2)

/** struct vmmdev_hypervisorinfo - Hypervisor info structure. */
struct vmmdev_hypervisorinfo {
	/** Header. */
	struct vmmdev_request_header header;
	/**
	 * Guest virtual address of proposed hypervisor start.
	 * Not used by VMMDEVREQ_GET_HYPERVISOR_INFO.
	 */
	u32 hypervisor_start;
	/** Hypervisor size in bytes. */
	u32 hypervisor_size;
};
VMMDEV_ASSERT_SIZE(vmmdev_hypervisorinfo, 24 + 8);

/** struct vmmdev_events - Pending events structure. */
struct vmmdev_events {
	/** Header. */
	struct vmmdev_request_header header;
	/** OUT: Pending event mask. */
	u32 events;
};
VMMDEV_ASSERT_SIZE(vmmdev_events, 24 + 4);

#define VMMDEV_OSTYPE_LINUX26		0x53000
#define VMMDEV_OSTYPE_X64		BIT(8)

/** struct vmmdev_guestinfo - Guest information report. */
struct vmmdev_guest_info {
	/** Header. */
	struct vmmdev_request_header header;
	/**
	 * The VMMDev interface version expected by additions.
	 * *Deprecated*, do not use anymore! Will be removed.
	 */
	u32 interface_version;
	/** Guest OS type. */
	u32 os_type;
};
VMMDEV_ASSERT_SIZE(vmmdev_guest_info, 24 + 8);

#define VMMDEV_GUEST_INFO2_ADDITIONS_FEATURES_REQUESTOR_INFO	BIT(0)

/** struct vmmdev_guestinfo2 - Guest information report, version 2. */
struct vmmdev_guest_info2 {
	/** Header. */
	struct vmmdev_request_header header;
	/** Major version. */
	u16 additions_major;
	/** Minor version. */
	u16 additions_minor;
	/** Build number. */
	u32 additions_build;
	/** SVN revision. */
	u32 additions_revision;
	/** Feature mask. */
	u32 additions_features;
	/**
	 * The intentional meaning of this field was:
	 * Some additional information, for example 'Beta 1' or something like
	 * that.
	 *
	 * The way it was implemented was implemented: VBG_VERSION_STRING.
	 *
	 * This means the first three members are duplicated in this field (if
	 * the guest build config is sane). So, the user must check this and
	 * chop it off before usage. There is, because of the Main code's blind
	 * trust in the field's content, no way back.
	 */
	char name[128];
};
VMMDEV_ASSERT_SIZE(vmmdev_guest_info2, 24 + 144);

enum vmmdev_guest_facility_type {
	VBOXGUEST_FACILITY_TYPE_UNKNOWN          = 0,
	VBOXGUEST_FACILITY_TYPE_VBOXGUEST_DRIVER = 20,
	/* VBoxGINA / VBoxCredProv / pam_vbox. */
	VBOXGUEST_FACILITY_TYPE_AUTO_LOGON       = 90,
	VBOXGUEST_FACILITY_TYPE_VBOX_SERVICE     = 100,
	/* VBoxTray (Windows), VBoxClient (Linux, Unix). */
	VBOXGUEST_FACILITY_TYPE_VBOX_TRAY_CLIENT = 101,
	VBOXGUEST_FACILITY_TYPE_SEAMLESS         = 1000,
	VBOXGUEST_FACILITY_TYPE_GRAPHICS         = 1100,
	VBOXGUEST_FACILITY_TYPE_ALL              = 0x7ffffffe,
	/* Ensure the enum is a 32 bit data-type */
	VBOXGUEST_FACILITY_TYPE_SIZEHACK         = 0x7fffffff
};

enum vmmdev_guest_facility_status {
	VBOXGUEST_FACILITY_STATUS_INACTIVE    = 0,
	VBOXGUEST_FACILITY_STATUS_PAUSED      = 1,
	VBOXGUEST_FACILITY_STATUS_PRE_INIT    = 20,
	VBOXGUEST_FACILITY_STATUS_INIT        = 30,
	VBOXGUEST_FACILITY_STATUS_ACTIVE      = 50,
	VBOXGUEST_FACILITY_STATUS_TERMINATING = 100,
	VBOXGUEST_FACILITY_STATUS_TERMINATED  = 101,
	VBOXGUEST_FACILITY_STATUS_FAILED      = 800,
	VBOXGUEST_FACILITY_STATUS_UNKNOWN     = 999,
	/* Ensure the enum is a 32 bit data-type */
	VBOXGUEST_FACILITY_STATUS_SIZEHACK    = 0x7fffffff
};

/** struct vmmdev_guest_status - Guest Additions status structure. */
struct vmmdev_guest_status {
	/** Header. */
	struct vmmdev_request_header header;
	/** Facility the status is indicated for. */
	enum vmmdev_guest_facility_type facility;
	/** Current guest status. */
	enum vmmdev_guest_facility_status status;
	/** Flags, not used at the moment. */
	u32 flags;
};
VMMDEV_ASSERT_SIZE(vmmdev_guest_status, 24 + 12);

#define VMMDEV_MEMORY_BALLOON_CHUNK_SIZE             (1048576)
#define VMMDEV_MEMORY_BALLOON_CHUNK_PAGES            (1048576 / 4096)

/** struct vmmdev_memballoon_info - Memory-balloon info structure. */
struct vmmdev_memballoon_info {
	/** Header. */
	struct vmmdev_request_header header;
	/** Balloon size in megabytes. */
	u32 balloon_chunks;
	/** Guest ram size in megabytes. */
	u32 phys_mem_chunks;
	/**
	 * Setting this to VMMDEV_EVENT_BALLOON_CHANGE_REQUEST indicates that
	 * the request is a response to that event.
	 * (Don't confuse this with VMMDEVREQ_ACKNOWLEDGE_EVENTS.)
	 */
	u32 event_ack;
};
VMMDEV_ASSERT_SIZE(vmmdev_memballoon_info, 24 + 12);

/** struct vmmdev_memballoon_change - Change the size of the balloon. */
struct vmmdev_memballoon_change {
	/** Header. */
	struct vmmdev_request_header header;
	/** The number of pages in the array. */
	u32 pages;
	/** true = inflate, false = deflate.  */
	u32 inflate;
	/** Physical address (u64) of each page. */
	u64 phys_page[VMMDEV_MEMORY_BALLOON_CHUNK_PAGES];
};

/** struct vmmdev_write_core_dump - Write Core Dump request data. */
struct vmmdev_write_core_dump {
	/** Header. */
	struct vmmdev_request_header header;
	/** Flags (reserved, MBZ). */
	u32 flags;
};
VMMDEV_ASSERT_SIZE(vmmdev_write_core_dump, 24 + 4);

/** struct vmmdev_heartbeat - Heart beat check state structure. */
struct vmmdev_heartbeat {
	/** Header. */
	struct vmmdev_request_header header;
	/** OUT: Guest heartbeat interval in nanosec. */
	u64 interval_ns;
	/** Heartbeat check flag. */
	u8 enabled;
	/** Explicit padding, MBZ. */
	u8 padding[3];
} __packed;
VMMDEV_ASSERT_SIZE(vmmdev_heartbeat, 24 + 12);

#define VMMDEV_HGCM_REQ_DONE      BIT(0)
#define VMMDEV_HGCM_REQ_CANCELLED BIT(1)

/** struct vmmdev_hgcmreq_header - vmmdev HGCM requests header. */
struct vmmdev_hgcmreq_header {
	/** Request header. */
	struct vmmdev_request_header header;

	/** HGCM flags. */
	u32 flags;

	/** Result code. */
	s32 result;
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcmreq_header, 24 + 8);

/** struct vmmdev_hgcm_connect - HGCM connect request structure. */
struct vmmdev_hgcm_connect {
	/** HGCM request header. */
	struct vmmdev_hgcmreq_header header;

	/** IN: Description of service to connect to. */
	struct vmmdev_hgcm_service_location loc;

	/** OUT: Client identifier assigned by local instance of HGCM. */
	u32 client_id;
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_connect, 32 + 132 + 4);

/** struct vmmdev_hgcm_disconnect - HGCM disconnect request structure. */
struct vmmdev_hgcm_disconnect {
	/** HGCM request header. */
	struct vmmdev_hgcmreq_header header;

	/** IN: Client identifier. */
	u32 client_id;
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_disconnect, 32 + 4);

#define VMMDEV_HGCM_MAX_PARMS 32

/** struct vmmdev_hgcm_call - HGCM call request structure. */
struct vmmdev_hgcm_call {
	/* request header */
	struct vmmdev_hgcmreq_header header;

	/** IN: Client identifier. */
	u32 client_id;
	/** IN: Service function number. */
	u32 function;
	/** IN: Number of parameters. */
	u32 parm_count;
	/** Parameters follow in form: HGCMFunctionParameter32|64 parms[X]; */
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_call, 32 + 12);

/**
 * struct vmmdev_hgcm_cancel2 - HGCM cancel request structure, version 2.
 *
 * After the request header.rc will be:
 *
 * VINF_SUCCESS when cancelled.
 * VERR_NOT_FOUND if the specified request cannot be found.
 * VERR_INVALID_PARAMETER if the address is invalid valid.
 */
struct vmmdev_hgcm_cancel2 {
	/** Header. */
	struct vmmdev_request_header header;
	/** The physical address of the request to cancel. */
	u32 phys_req_to_cancel;
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_cancel2, 24 + 4);

#endif
