/*
** $Id: ldo.h,v 2.29.1.1 2017/04/19 17:20:42 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/
#define luaD_checkstackaux(L,n,pre,pos)  \
	if (L->stack_last - L->top <= (n)) \
	  { pre; luaD_growstack(L, n); pos; } else { condmovestack(L,pre,pos); }

/* In general, 'pre'/'pos' are empty (nothing to save) */
#define luaD_checkstack(L,n)	luaD_checkstackaux(L,n,(void)0,(void)0)



#define savestack(L,p)		((char *)(p) - (char *)L->stack)
#define restorestack(L,n)	((TValue *)((char *)L->stack + (n)))


/* type of protected functions, to be ran by 'runprotected' */
typedef void (*Pfunc) (lua_State *L, void *ud);

LUAI_FUNC int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                                  const char *mode);
LUAI_FUNC void luaD_hook (lua_State *L, int event, int line);
LUAI_FUNC int luaD_precall (lua_State *L, StkId func, int nresults);
LUAI_FUNC void luaD_call (lua_State *L, StkId func, int nResults);
LUAI_FUNC void luaD_callnoyield (lua_State *L, StkId func, int nResults);
LUAI_FUNC int luaD_pcall (lua_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
LUAI_FUNC int luaD_poscall (lua_State *L, CallInfo *ci, StkId firstResult,
                                          int nres);
LUAI_FUNC void luaD_reallocstack (lua_State *L, int newsize);
LUAI_FUNC void luaD_growstack (lua_State *L, int n);
LUAI_FUNC void luaD_shrinkstack (lua_State *L);
LUAI_FUNC void luaD_inctop (lua_State *L);

LUAI_FUNC l_noret luaD_throw (lua_State *L, int errcode);
LUAI_FUNC int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud);

#endif

