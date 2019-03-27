/* C99 nested struct init with named and non-named initializers */
typedef struct pthread_mutex_t {
 unsigned int ptm_magic;
 char ptm_errorcheck;

 char ptm_pad1[3];

 char ptm_interlock;

 char ptm_pad2[3];

 volatile void * ptm_owner;
 void * volatile ptm_waiters;
 unsigned int ptm_recursed;
 void *ptm_spare2;
} pthread_mutex_t;


struct arc4random_global {

 pthread_mutex_t lock;
} arc4random_global = {

 .lock = { 0x33330003, 0, { 0, 0, 0 }, 0, { 0, 0, 0 }, ((void *)0), ((void *)0), 0, ((void *)0) },
};
