#ifndef CFQ_IOSCHED_H
#define CFQ_IOSCHED_H

void cfq_unlink_blkio_group(void *, struct blkio_group *);
void cfq_update_blkio_group_weight(struct blkio_group *, unsigned int);

#endif
