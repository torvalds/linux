#ifndef __NVIF_IF000C_H__
#define __NVIF_IF000C_H__
struct nvif_vmm_v0 {
	__u8  version;
	__u8  page_nr;
	__u8  managed;
	__u8  pad03[5];
	__u64 addr;
	__u64 size;
	__u8  data[];
};

#define NVIF_VMM_V0_PAGE                                                   0x00
#define NVIF_VMM_V0_GET                                                    0x01
#define NVIF_VMM_V0_PUT                                                    0x02
#define NVIF_VMM_V0_MAP                                                    0x03
#define NVIF_VMM_V0_UNMAP                                                  0x04
#define NVIF_VMM_V0_PFNMAP                                                 0x05
#define NVIF_VMM_V0_PFNCLR                                                 0x06
#define NVIF_VMM_V0_MTHD(i)                                         ((i) + 0x80)

struct nvif_vmm_page_v0 {
	__u8  version;
	__u8  index;
	__u8  shift;
	__u8  sparse;
	__u8  vram;
	__u8  host;
	__u8  comp;
	__u8  pad07[1];
};

struct nvif_vmm_get_v0 {
	__u8  version;
#define NVIF_VMM_GET_V0_ADDR                                               0x00
#define NVIF_VMM_GET_V0_PTES                                               0x01
#define NVIF_VMM_GET_V0_LAZY	                                           0x02
	__u8  type;
	__u8  sparse;
	__u8  page;
	__u8  align;
	__u8  pad05[3];
	__u64 size;
	__u64 addr;
};

struct nvif_vmm_put_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
};

struct nvif_vmm_map_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
	__u64 size;
	__u64 memory;
	__u64 offset;
	__u8  data[];
};

struct nvif_vmm_unmap_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
};

struct nvif_vmm_pfnmap_v0 {
	__u8  version;
	__u8  page;
	__u8  pad02[6];
	__u64 addr;
	__u64 size;
#define NVIF_VMM_PFNMAP_V0_ADDR                           0xfffffffffffff000ULL
#define NVIF_VMM_PFNMAP_V0_ADDR_SHIFT                                        12
#define NVIF_VMM_PFNMAP_V0_APER                           0x00000000000000f0ULL
#define NVIF_VMM_PFNMAP_V0_HOST                           0x0000000000000000ULL
#define NVIF_VMM_PFNMAP_V0_VRAM                           0x0000000000000010ULL
#define NVIF_VMM_PFNMAP_V0_A				  0x0000000000000004ULL
#define NVIF_VMM_PFNMAP_V0_W                              0x0000000000000002ULL
#define NVIF_VMM_PFNMAP_V0_V                              0x0000000000000001ULL
#define NVIF_VMM_PFNMAP_V0_NONE                           0x0000000000000000ULL
	__u64 phys[];
};

struct nvif_vmm_pfnclr_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
	__u64 size;
};
#endif
