#define RT_PEND_TQ_SIZE 16
struct rt_pend_tq {
	void (*func) (int arg1, void *arg2);
	int arg1;
	void *arg2;
};
extern int rt_pend_call(void (*func) (int arg1, void *arg2), int arg1,
	void *arg2);
extern int rt_pend_tq_init(void);
extern void rt_pend_tq_cleanup(void);
