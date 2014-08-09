#ifndef __NVIF_EVENT_H__
#define __NVIF_EVENT_H__

struct nvif_notify_head_req_v0 {
	__u8  version;
	__u8  head;
};

struct nvif_notify_head_rep_v0 {
	__u8  version;
};

struct nvif_notify_conn_req_v0 {
	__u8  version;
#define NVIF_NOTIFY_CONN_V0_PLUG                                           0x01
#define NVIF_NOTIFY_CONN_V0_UNPLUG                                         0x02
#define NVIF_NOTIFY_CONN_V0_IRQ                                            0x04
#define NVIF_NOTIFY_CONN_V0_ANY                                            0x07
	__u8  mask;
	__u8  conn;
};

struct nvif_notify_conn_rep_v0 {
	__u8  version;
	__u8  mask;
};

#endif
