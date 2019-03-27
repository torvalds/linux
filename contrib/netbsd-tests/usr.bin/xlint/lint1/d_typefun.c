/* typedef of function parameter */

typedef void (*free_func) (void * opaque, void* address);
typedef struct stack_st
{
 int num;
 char **data;
 int sorted;

 int num_alloc;
 int (*comp)(const void *, const void *);
} _STACK; /* Use STACK_OF(...) instead */

typedef void *OPENSSL_BLOCK;
struct stack_st_OPENSSL_BLOCK { _STACK stack; };
typedef void *d2i_of_void(void **,const unsigned char **,long); typedef int i2d_of_void(void *,unsigned char **);

struct stack_st_OPENSSL_BLOCK *d2i_ASN1_SET(struct stack_st_OPENSSL_BLOCK **a,
         const unsigned char **pp,
         long length, d2i_of_void *d2i,
         void (*free_func)(OPENSSL_BLOCK), int ex_tag,
         int ex_class);
