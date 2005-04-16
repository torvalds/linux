#ifndef __CMP_H
#define __CMP_H

struct cmp_mpr {
	u32 nplugs:5;
	u32 reserved:3;
	u32 persistent_ext:8;
	u32 non_persistent_ext:8;
	u32 bcast_channel_base:6;
	u32 rate:2;
} __attribute__((packed));

struct cmp_pcr {
	u32 payload:10;
	u32 overhead:4;
	u32 speed:2;
	u32 channel:6;
	u32 reserved:2;
	u32 p2p_count:6;
	u32 bcast_count:1;
	u32 online:1;
} __attribute__((packed));

struct cmp_pcr *cmp_register_opcr(struct hpsb_host *host, int plug,
				  int payload,
				  void (*update)(struct cmp_pcr *plug,
						 void *data),
				  void *data);
void cmp_unregister_opcr(struct hpsb_host *host, struct cmp_pcr *plug);

#endif /* __CMP_H */
