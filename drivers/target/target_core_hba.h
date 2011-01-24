#ifndef TARGET_CORE_HBA_H
#define TARGET_CORE_HBA_H

extern struct se_hba *core_alloc_hba(const char *, u32, u32);
extern int core_delete_hba(struct se_hba *);

#endif /* TARGET_CORE_HBA_H */
