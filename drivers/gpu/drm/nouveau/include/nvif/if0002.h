#ifndef __NVIF_IF0002_H__
#define __NVIF_IF0002_H__

#define NVIF_PERFMON_V0_QUERY_DOMAIN                                       0x00
#define NVIF_PERFMON_V0_QUERY_SIGNAL                                       0x01
#define NVIF_PERFMON_V0_QUERY_SOURCE                                       0x02

struct nvif_perfmon_query_domain_v0 {
	__u8  version;
	__u8  id;
	__u8  counter_nr;
	__u8  iter;
	__u16 signal_nr;
	__u8  pad05[2];
	char  name[64];
};

struct nvif_perfmon_query_signal_v0 {
	__u8  version;
	__u8  domain;
	__u16 iter;
	__u8  signal;
	__u8  source_nr;
	__u8  pad05[2];
	char  name[64];
};

struct nvif_perfmon_query_source_v0 {
	__u8  version;
	__u8  domain;
	__u8  signal;
	__u8  iter;
	__u8  pad04[4];
	__u32 source;
	__u32 mask;
	char  name[64];
};
#endif
