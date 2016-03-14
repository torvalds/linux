#include <scsi/scsi_tgt.h>

#ifdef CONFIG_SCSI_FC_TGT_ATTRS
static inline int fc_tgt_it_nexus_create(struct Scsi_Host *shost, u64 itn_id,
					 char *initiator)
{
	return scsi_tgt_it_nexus_create(shost, itn_id, initiator);
}

static inline int fc_tgt_it_nexus_destroy(struct Scsi_Host *shost, u64 itn_id)
{
	return scsi_tgt_it_nexus_destroy(shost, itn_id);
}
#else
static inline int fc_tgt_it_nexus_create(struct Scsi_Host *shost, u64 itn_id,
					 char *initiator)
{
	return 0;
}

static inline int fc_tgt_it_nexus_destroy(struct Scsi_Host *shost, u64 itn_id)
{
	return 0;
}

#endif
