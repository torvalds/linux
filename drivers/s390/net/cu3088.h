#ifndef _CU3088_H
#define _CU3088_H

/**
 * Enum for classifying detected devices.
 */
enum channel_types {
        /* Device is not a channel  */
	channel_type_none,

        /* Device is a CTC/A */
	channel_type_parallel,

	/* Device is a ESCON channel */
	channel_type_escon,

	/* Device is a FICON channel */
	channel_type_ficon,

	/* Device is a P390 LCS card */
	channel_type_p390,

	/* Device is a OSA2 card */
	channel_type_osa2,

	/* Device is a channel, but we don't know
	 * anything about it */
	channel_type_unknown,

	/* Device is an unsupported model */
	channel_type_unsupported,

	/* number of type entries */
	num_channel_types
};

extern const char *cu3088_type[num_channel_types];
extern int register_cu3088_discipline(struct ccwgroup_driver *);
extern void unregister_cu3088_discipline(struct ccwgroup_driver *);

#endif
