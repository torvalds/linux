#ifndef __NVIF_EVENT_H__
#define __NVIF_EVENT_H__

struct nvif_notify_req_v0 {
	__u8  version;
	__u8  reply;
	__u8  pad02[5];
#define NVIF_NOTIFY_V0_ROUTE_NVIF                                          0x00
	__u8  route;
	__u64 token;	/* must be unique */
	__u8  data[];	/* request data (below) */
};

struct nvif_notify_rep_v0 {
	__u8  version;
	__u8  pad01[6];
	__u8  route;
	__u64 token;
	__u8  data[];	/* reply data (below) */
};

struct nvif_notify_head_req_v0 {
	/* nvif_notify_req ... */
	__u8  version;
	__u8  head;
	__u8  pad02[6];
};

struct nvif_notify_head_rep_v0 {
	/* nvif_notify_rep ... */
	__u8  version;
	__u8  pad01[7];
};

struct nvif_notify_conn_req_v0 {
	/* nvif_notify_req ... */
	__u8  version;
#define NVIF_NOTIFY_CONN_V0_PLUG                                           0x01
#define NVIF_NOTIFY_CONN_V0_UNPLUG                                         0x02
#define NVIF_NOTIFY_CONN_V0_IRQ                                            0x04
#define NVIF_NOTIFY_CONN_V0_ANY                                            0x07
	__u8  mask;
	__u8  conn;
	__u8  pad03[5];
};

struct nvif_notify_conn_rep_v0 {
	/* nvif_notify_rep ... */
	__u8  version;
	__u8  mask;
	__u8  pad02[6];
};

struct nvif_notify_uevent_req {
	/* nvif_notify_req ... */
};

struct nvif_notify_uevent_rep {
	/* nvif_notify_rep ... */
};

#endif
