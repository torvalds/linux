#ifndef _XEN_P2M_H
#define _XEN_P2M_H

#define P2M_PER_PAGE        (PAGE_SIZE / sizeof(unsigned long))
#define P2M_MID_PER_PAGE    (PAGE_SIZE / sizeof(unsigned long *))
#define P2M_TOP_PER_PAGE    (PAGE_SIZE / sizeof(unsigned long **))

#define MAX_P2M_PFN         (P2M_TOP_PER_PAGE * P2M_MID_PER_PAGE * P2M_PER_PAGE)

#define MAX_REMAP_RANGES    10

extern unsigned long __init set_phys_range_identity(unsigned long pfn_s,
                                      unsigned long pfn_e);

#endif  /* _XEN_P2M_H */
