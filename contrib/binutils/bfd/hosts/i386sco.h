/* Core file stuff.  At least some, perhaps all, of the following
   defines work on many more systems than just SCO.  */

#define NBPG NBPC
#define UPAGES USIZE
#define HOST_DATA_START_ADDR u.u_exdata.ux_datorg
#define HOST_STACK_START_ADDR u.u_sub
#define TRAD_UNIX_CORE_FILE_FAILING_SIGNAL(abfd) \
  ((core_upage(abfd)->u_sysabort != 0) \
   ? core_upage(abfd)->u_sysabort \
   : -1)

/* According to the manpage, a version 2 SCO corefile can contain
   various additional sections (it is cleverly arranged so the u area,
   data, and stack are first where we can find them).  So without
   writing lots of code to parse all their headers and stuff, we can't
   know whether a corefile is bigger than it should be.  */

#define TRAD_CORE_ALLOW_ANY_EXTRA_SIZE 1
