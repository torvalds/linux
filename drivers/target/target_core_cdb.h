#ifndef TARGET_CORE_CDB_H
#define TARGET_CORE_CDB_H

int target_emulate_inquiry(struct se_task *task);
int target_emulate_readcapacity(struct se_task *task);
int target_emulate_readcapacity_16(struct se_task *task);
int target_emulate_modesense(struct se_task *task);
int target_emulate_request_sense(struct se_task *task);
int target_emulate_unmap(struct se_task *task);
int target_emulate_write_same(struct se_task *task);
int target_emulate_synchronize_cache(struct se_task *task);
int target_emulate_noop(struct se_task *task);

#endif /* TARGET_CORE_CDB_H */
