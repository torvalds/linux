/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef KFD_DBGMGR_H_
#define KFD_DBGMGR_H_

#include "kfd_priv.h"

/* must align with hsakmttypes definition */
#pragma pack(push, 4)

enum HSA_DBG_WAVEOP {
	HSA_DBG_WAVEOP_HALT = 1,   /* Halts a wavefront */
	HSA_DBG_WAVEOP_RESUME = 2, /* Resumes a wavefront */
	HSA_DBG_WAVEOP_KILL = 3,   /* Kills a wavefront */
	HSA_DBG_WAVEOP_DEBUG = 4,  /* Causes wavefront to enter dbg mode */
	HSA_DBG_WAVEOP_TRAP = 5,   /* Causes wavefront to take a trap */
	HSA_DBG_NUM_WAVEOP = 5,
	HSA_DBG_MAX_WAVEOP = 0xFFFFFFFF
};

enum HSA_DBG_WAVEMODE {
	/* send command to a single wave */
	HSA_DBG_WAVEMODE_SINGLE = 0,
	/*
	 * Broadcast to all wavefronts of all processes is not
	 * supported for HSA user mode
	 */

	/* send to waves within current process */
	HSA_DBG_WAVEMODE_BROADCAST_PROCESS = 2,
	/* send to waves within current process on CU  */
	HSA_DBG_WAVEMODE_BROADCAST_PROCESS_CU = 3,
	HSA_DBG_NUM_WAVEMODE = 3,
	HSA_DBG_MAX_WAVEMODE = 0xFFFFFFFF
};

enum HSA_DBG_WAVEMSG_TYPE {
	HSA_DBG_WAVEMSG_AUTO = 0,
	HSA_DBG_WAVEMSG_USER = 1,
	HSA_DBG_WAVEMSG_ERROR = 2,
	HSA_DBG_NUM_WAVEMSG,
	HSA_DBG_MAX_WAVEMSG = 0xFFFFFFFF
};

enum HSA_DBG_WATCH_MODE {
	HSA_DBG_WATCH_READ = 0,		/* Read operations only */
	HSA_DBG_WATCH_NONREAD = 1,	/* Write or Atomic operations only */
	HSA_DBG_WATCH_ATOMIC = 2,	/* Atomic Operations only */
	HSA_DBG_WATCH_ALL = 3,		/* Read, Write or Atomic operations */
	HSA_DBG_WATCH_NUM,
	HSA_DBG_WATCH_SIZE = 0xFFFFFFFF
};

/* This structure is hardware specific and may change in the future */
struct HsaDbgWaveMsgAMDGen2 {
	union {
		struct ui32 {
			uint32_t UserData:8;	/* user data */
			uint32_t ShaderArray:1;	/* Shader array */
			uint32_t Priv:1;	/* Privileged */
			uint32_t Reserved0:4;	/* Reserved, should be 0 */
			uint32_t WaveId:4;	/* wave id */
			uint32_t SIMD:2;	/* SIMD id */
			uint32_t HSACU:4;	/* Compute unit */
			uint32_t ShaderEngine:2;/* Shader engine */
			uint32_t MessageType:2;	/* see HSA_DBG_WAVEMSG_TYPE */
			uint32_t Reserved1:4;	/* Reserved, should be 0 */
		} ui32;
		uint32_t Value;
	};
	uint32_t Reserved2;
};

union HsaDbgWaveMessageAMD {
	struct HsaDbgWaveMsgAMDGen2 WaveMsgInfoGen2;
	/* for future HsaDbgWaveMsgAMDGen3; */
};

struct HsaDbgWaveMessage {
	void *MemoryVA;		/* ptr to associated host-accessible data */
	union HsaDbgWaveMessageAMD DbgWaveMsg;
};

/*
 * TODO: This definitions to be MOVED to kfd_event, once it is implemented.
 *
 * HSA sync primitive, Event and HW Exception notification API definitions.
 * The API functions allow the runtime to define a so-called sync-primitive,
 * a SW object combining a user-mode provided "syncvar" and a scheduler event
 * that can be signaled through a defined GPU interrupt. A syncvar is
 * a process virtual memory location of a certain size that can be accessed
 * by CPU and GPU shader code within the process to set and query the content
 * within that memory. The definition of the content is determined by the HSA
 * runtime and potentially GPU shader code interfacing with the HSA runtime.
 * The syncvar values may be commonly written through an PM4 WRITE_DATA packet
 * in the user mode instruction stream. The OS scheduler event is typically
 * associated and signaled by an interrupt issued by the GPU, but other HSA
 * system interrupt conditions from other HW (e.g. IOMMUv2) may be surfaced
 * by the KFD by this mechanism, too.
 */

/* these are the new definitions for events */
enum HSA_EVENTTYPE {
	HSA_EVENTTYPE_SIGNAL = 0,	/* user-mode generated GPU signal */
	HSA_EVENTTYPE_NODECHANGE = 1,	/* HSA node change (attach/detach) */
	HSA_EVENTTYPE_DEVICESTATECHANGE = 2,	/* HSA device state change
						 * (start/stop)
						 */
	HSA_EVENTTYPE_HW_EXCEPTION = 3,	/* GPU shader exception event */
	HSA_EVENTTYPE_SYSTEM_EVENT = 4,	/* GPU SYSCALL with parameter info */
	HSA_EVENTTYPE_DEBUG_EVENT = 5,	/* GPU signal for debugging */
	HSA_EVENTTYPE_PROFILE_EVENT = 6,/* GPU signal for profiling */
	HSA_EVENTTYPE_QUEUE_EVENT = 7,	/* GPU signal queue idle state
					 * (EOP pm4)
					 */
	/* ...  */
	HSA_EVENTTYPE_MAXID,
	HSA_EVENTTYPE_TYPE_SIZE = 0xFFFFFFFF
};

/* Sub-definitions for various event types: Syncvar */
struct HsaSyncVar {
	union SyncVar {
		void *UserData;	/* pointer to user mode data */
		uint64_t UserDataPtrValue; /* 64bit compatibility of value */
	} SyncVar;
	uint64_t SyncVarSize;
};

/* Sub-definitions for various event types: NodeChange */

enum HSA_EVENTTYPE_NODECHANGE_FLAGS {
	HSA_EVENTTYPE_NODECHANGE_ADD = 0,
	HSA_EVENTTYPE_NODECHANGE_REMOVE = 1,
	HSA_EVENTTYPE_NODECHANGE_SIZE = 0xFFFFFFFF
};

struct HsaNodeChange {
	/* HSA node added/removed on the platform */
	enum HSA_EVENTTYPE_NODECHANGE_FLAGS Flags;
};

/* Sub-definitions for various event types: DeviceStateChange */
enum HSA_EVENTTYPE_DEVICESTATECHANGE_FLAGS {
	/* device started (and available) */
	HSA_EVENTTYPE_DEVICESTATUSCHANGE_START = 0,
	/* device stopped (i.e. unavailable) */
	HSA_EVENTTYPE_DEVICESTATUSCHANGE_STOP = 1,
	HSA_EVENTTYPE_DEVICESTATUSCHANGE_SIZE = 0xFFFFFFFF
};

enum HSA_DEVICE {
	HSA_DEVICE_CPU = 0,
	HSA_DEVICE_GPU = 1,
	MAX_HSA_DEVICE = 2
};

struct HsaDeviceStateChange {
	uint32_t NodeId;	/* F-NUMA node that contains the device */
	enum HSA_DEVICE Device;	/* device type: GPU or CPU */
	enum HSA_EVENTTYPE_DEVICESTATECHANGE_FLAGS Flags; /* event flags */
};

struct HsaEventData {
	enum HSA_EVENTTYPE EventType; /* event type */
	union EventData {
		/*
		 * return data associated with HSA_EVENTTYPE_SIGNAL
		 * and other events
		 */
		struct HsaSyncVar SyncVar;

		/* data associated with HSA_EVENTTYPE_NODE_CHANGE */
		struct HsaNodeChange NodeChangeState;

		/* data associated with HSA_EVENTTYPE_DEVICE_STATE_CHANGE */
		struct HsaDeviceStateChange DeviceState;
	} EventData;

	/* the following data entries are internal to the KFD & thunk itself */

	/* internal thunk store for Event data (OsEventHandle) */
	uint64_t HWData1;
	/* internal thunk store for Event data (HWAddress) */
	uint64_t HWData2;
	/* internal thunk store for Event data (HWData) */
	uint32_t HWData3;
};

struct HsaEventDescriptor {
	/* event type to allocate */
	enum HSA_EVENTTYPE EventType;
	/* H-NUMA node containing GPU device that is event source */
	uint32_t NodeId;
	/* pointer to user mode syncvar data, syncvar->UserDataPtrValue
	 * may be NULL
	 */
	struct HsaSyncVar SyncVar;
};

struct HsaEvent {
	uint32_t EventId;
	struct HsaEventData EventData;
};

#pragma pack(pop)

enum DBGDEV_TYPE {
	DBGDEV_TYPE_ILLEGAL = 0,
	DBGDEV_TYPE_NODIQ = 1,
	DBGDEV_TYPE_DIQ = 2,
	DBGDEV_TYPE_TEST = 3
};

struct dbg_address_watch_info {
	struct kfd_process *process;
	enum HSA_DBG_WATCH_MODE *watch_mode;
	uint64_t *watch_address;
	uint64_t *watch_mask;
	struct HsaEvent *watch_event;
	uint32_t num_watch_points;
};

struct dbg_wave_control_info {
	struct kfd_process *process;
	uint32_t trapId;
	enum HSA_DBG_WAVEOP operand;
	enum HSA_DBG_WAVEMODE mode;
	struct HsaDbgWaveMessage dbgWave_msg;
};

struct kfd_dbgdev {

	/* The device that owns this data. */
	struct kfd_dev *dev;

	/* kernel queue for DIQ */
	struct kernel_queue *kq;

	/* a pointer to the pqm of the calling process */
	struct process_queue_manager *pqm;

	/* type of debug device ( DIQ, non DIQ, etc. ) */
	enum DBGDEV_TYPE type;

	/* virtualized function pointers to device dbg */
	int (*dbgdev_register)(struct kfd_dbgdev *dbgdev);
	int (*dbgdev_unregister)(struct kfd_dbgdev *dbgdev);
	int (*dbgdev_address_watch)(struct kfd_dbgdev *dbgdev,
				struct dbg_address_watch_info *adw_info);
	int (*dbgdev_wave_control)(struct kfd_dbgdev *dbgdev,
				struct dbg_wave_control_info *wac_info);

};

struct kfd_dbgmgr {
	u32 pasid;
	struct kfd_dev *dev;
	struct kfd_dbgdev *dbgdev;
};

/* prototypes for debug manager functions */
struct mutex *kfd_get_dbgmgr_mutex(void);
void kfd_dbgmgr_destroy(struct kfd_dbgmgr *pmgr);
bool kfd_dbgmgr_create(struct kfd_dbgmgr **ppmgr, struct kfd_dev *pdev);
long kfd_dbgmgr_register(struct kfd_dbgmgr *pmgr, struct kfd_process *p);
long kfd_dbgmgr_unregister(struct kfd_dbgmgr *pmgr, struct kfd_process *p);
long kfd_dbgmgr_wave_control(struct kfd_dbgmgr *pmgr,
				struct dbg_wave_control_info *wac_info);
long kfd_dbgmgr_address_watch(struct kfd_dbgmgr *pmgr,
			struct dbg_address_watch_info *adw_info);
#endif /* KFD_DBGMGR_H_ */
