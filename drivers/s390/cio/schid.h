#ifndef S390_SCHID_H
#define S390_SCHID_H

struct subchannel_id {
	__u32 reserved:13;
	__u32 ssid:2;
	__u32 one:1;
	__u32 sch_no:16;
} __attribute__ ((packed,aligned(4)));


/* Helper function for sane state of pre-allocated subchannel_id. */
static inline void
init_subchannel_id(struct subchannel_id *schid)
{
	memset(schid, 0, sizeof(struct subchannel_id));
	schid->one = 1;
}

static inline int
schid_equal(struct subchannel_id *schid1, struct subchannel_id *schid2)
{
	return !memcmp(schid1, schid2, sizeof(struct subchannel_id));
}

#endif /* S390_SCHID_H */
