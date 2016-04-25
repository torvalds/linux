#ifndef __NVIF_IF0000_H__
#define __NVIF_IF0000_H__

#define NV_CLIENT_DEVLIST                                                  0x00

struct nv_client_devlist_v0 {
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	__u64 device[];
};
#endif
