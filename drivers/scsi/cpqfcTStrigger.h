// don't do this unless you have the right hardware!
#define TRIGGERABLE_HBA 0
#if TRIGGERABLE_HBA
void TriggerHBA( void*, int);
#else
#define TriggerHBA(x, y)
#endif

