/*
 *	For declaring structures shared with assembly routines
 *
 *	$Id: asstruct.h,v 1.1.1.1 1994/10/23 05:08:32 rick Exp $
 */

#ifdef ASSEMBLER

#	define MO(t,a)		(a)
#	define VMO(t,a)		(a)

#	define	BEGIN_STRUCT(x)	_Off=0
#	define	S1A(t,x,n)	_Off=(_Off+0)&~0; x=_Off; _Off=_Off+(1*n)
#	define	S2A(t,x,n)	_Off=(_Off+1)&~1; x=_Off; _Off=_Off+(2*n)
#	define	S4A(t,x,n)	_Off=(_Off+3)&~3; x=_Off; _Off=_Off+(4*n)
#	define	WORD(x)		_Off=(_Off+3)&~3; x=_Off; _Off=_Off+4
#	define	WORDA(x,n)	_Off=(_Off+3)&~3; x=_Off; _Off=_Off+(4*n)
#	define	VWORD(x)	_Off=(_Off+3)&~3; x=_Off; _Off=_Off+4
#	define	S1(t,x)		_Off=(_Off+0)&~0; x=_Off; _Off=_Off+1
#	define	S2(t,x)		_Off=(_Off+1)&~1; x=_Off; _Off=_Off+2
#	define	S4(t,x)		_Off=(_Off+3)&~3; x=_Off; _Off=_Off+4
#	define	END_STRUCT(x)	_Off=(_Off+3)&~3; x=_Off 

#else	/* C */

#define VMO(t,a)        (*(volatile t *)(a))

#	define BEGIN_STRUCT(x) struct x {
#	define S1(t,x)         t x ;
#	define S1A(t,x,n)      t x[n] ;
#	define S2(t,x)         t x ;
#	define S2A(t,x,n)      t x[n] ;
#	define S4(t,x)         t x ;
#	define S4A(t,x,n)      t x[n] ;
#	define END_STRUCT(x)   } ;

#endif
