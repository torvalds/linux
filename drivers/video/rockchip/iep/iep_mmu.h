#ifndef _IEP_MMU_H_
#define _IEP_MMU_H_

#include <linux/types.h>
#include "iep_drv.h"

struct rk_mmu_pte {
    int index;                      // dte entry index [0, 1023]
    uint32_t *pte;                  // point to pte table
    struct list_head session_link;  // link to session
};

int rk_mmu_generate_pte_from_va(iep_session *session, uint32_t va);
void rk_mmu_reclaim_pte_list(iep_session *session);
int rk_mmu_init_dte_table(iep_session *session);
void rk_mmu_deinit_dte_table(iep_session *session);

#endif
