#ifndef __NVIF_IOCTL_H__
#define __NVIF_IOCTL_H__

struct nvif_ioctl_v0 {
	__u8  version;
#define NVIF_IOCTL_V0_OWNER_NVIF                                           0x00
#define NVIF_IOCTL_V0_OWNER_ANY                                            0xff
	__u8  owner;
#define NVIF_IOCTL_V0_NOP                                                  0x00
#define NVIF_IOCTL_V0_SCLASS                                               0x01
#define NVIF_IOCTL_V0_NEW                                                  0x02
#define NVIF_IOCTL_V0_DEL                                                  0x03
#define NVIF_IOCTL_V0_MTHD                                                 0x04
#define NVIF_IOCTL_V0_RD                                                   0x05
#define NVIF_IOCTL_V0_WR                                                   0x06
#define NVIF_IOCTL_V0_MAP                                                  0x07
#define NVIF_IOCTL_V0_UNMAP                                                0x08
#define NVIF_IOCTL_V0_NTFY_NEW                                             0x09
#define NVIF_IOCTL_V0_NTFY_DEL                                             0x0a
#define NVIF_IOCTL_V0_NTFY_GET                                             0x0b
#define NVIF_IOCTL_V0_NTFY_PUT                                             0x0c
	__u8  type;
	__u8  path_nr;
#define NVIF_IOCTL_V0_ROUTE_NVIF                                           0x00
#define NVIF_IOCTL_V0_ROUTE_HIDDEN                                         0xff
	__u8  pad04[3];
	__u8  route;
	__u64 token;
	__u32 path[8];		/* in reverse */
	__u8  data[];		/* ioctl data (below) */
};

struct nvif_ioctl_nop {
};

struct nvif_ioctl_sclass_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	__u32 oclass[];
};

struct nvif_ioctl_new_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  pad01[6];
	__u8  route;
	__u64 token;
	__u32 handle;
/* these class numbers are made up by us, and not nvidia-assigned */
#define NVIF_IOCTL_NEW_V0_PERFCTR                                    0x0000ffff
#define NVIF_IOCTL_NEW_V0_CONTROL                                    0x0000fffe
	__u32 oclass;
	__u8  data[];		/* class data (class.h) */
};

struct nvif_ioctl_del {
};

struct nvif_ioctl_rd_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  size;
	__u8  pad02[2];
	__u32 data;
	__u64 addr;
};

struct nvif_ioctl_wr_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  size;
	__u8  pad02[2];
	__u32 data;
	__u64 addr;
};

struct nvif_ioctl_map_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  pad01[3];
	__u32 length;
	__u64 handle;
};

struct nvif_ioctl_unmap {
};

struct nvif_ioctl_ntfy_new_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  event;
	__u8  index;
	__u8  pad03[5];
	__u8  data[];		/* event request data (event.h) */
};

struct nvif_ioctl_ntfy_del_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  index;
	__u8  pad02[6];
};

struct nvif_ioctl_ntfy_get_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  index;
	__u8  pad02[6];
};

struct nvif_ioctl_ntfy_put_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  index;
	__u8  pad02[6];
};

struct nvif_ioctl_mthd_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  method;
	__u8  pad02[6];
	__u8  data[];		/* method data (class.h) */
};

#endif
