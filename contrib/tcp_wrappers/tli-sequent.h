#ifdef __STDC__
#define	__P(X) X
#else
#define	__P(X) ()
#endif

extern int t_sync __P((int));
extern char *t_alloc __P((int, int, int));
extern int t_free __P((char *, int));
extern int t_rcvudata __P((int, struct t_unitdata *, int *));
extern int getpeerinaddr __P((int, struct sockaddr_in *, int));
extern int getmyinaddr __P((int, struct sockaddr_in *, int));
extern struct _ti_user *_t_checkfd __P((int));
