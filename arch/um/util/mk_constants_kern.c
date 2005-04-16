#include "linux/kernel.h"
#include "linux/stringify.h"
#include "linux/time.h"
#include "asm/page.h"

extern void print_head(void);
extern void print_constant_str(char *name, char *value);
extern void print_constant_int(char *name, int value);
extern void print_tail(void);

int main(int argc, char **argv)
{
  print_head();
  print_constant_int("UM_KERN_PAGE_SIZE", PAGE_SIZE);

  print_constant_str("UM_KERN_EMERG", KERN_EMERG);
  print_constant_str("UM_KERN_ALERT", KERN_ALERT);
  print_constant_str("UM_KERN_CRIT", KERN_CRIT);
  print_constant_str("UM_KERN_ERR", KERN_ERR);
  print_constant_str("UM_KERN_WARNING", KERN_WARNING);
  print_constant_str("UM_KERN_NOTICE", KERN_NOTICE);
  print_constant_str("UM_KERN_INFO", KERN_INFO);
  print_constant_str("UM_KERN_DEBUG", KERN_DEBUG);

  print_constant_int("UM_NSEC_PER_SEC", NSEC_PER_SEC);
  print_tail();
  return(0);
}
