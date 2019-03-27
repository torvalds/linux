/****************************************************************************
 * Copyright (c) 1998-2010,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

/***************************************************************************
* Module m_hook                                                            *
* Assign application specific routines for automatic invocation by menus   *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_hook.c,v 1.16 2012/03/10 23:43:41 tom Exp $")

/* "Template" macro to generate function to set application specific hook */
#define GEN_HOOK_SET_FUNCTION( typ, name ) \
NCURSES_IMPEXP int NCURSES_API set_ ## typ ## _ ## name (MENU *menu, Menu_Hook func )\
{\
   T((T_CALLED("set_" #typ "_" #name "(%p,%p)"), (void *) menu, func));\
   (Normalize_Menu(menu) -> typ ## name = func );\
   RETURN(E_OK);\
}

/* "Template" macro to generate function to get application specific hook */
#define GEN_HOOK_GET_FUNCTION( typ, name ) \
NCURSES_IMPEXP Menu_Hook NCURSES_API typ ## _ ## name ( const MENU *menu )\
{\
   T((T_CALLED(#typ "_" #name "(%p)"), (const void *) menu));\
   returnMenuHook(Normalize_Menu(menu) -> typ ## name);\
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_menu_init(MENU *menu, void (*f)(MENU *))
|   
|   Description   :  Set user-exit which is called when menu is posted
|                    or just after the top row changes.
|
|   Return Values :  E_OK               - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(menu, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void (*)(MENU *) menu_init(const MENU *menu)
|   
|   Description   :  Return address of user-exit function which is called
|                    when a menu is posted or just after the top row 
|                    changes.
|
|   Return Values :  Menu init function address or NULL
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(menu, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_menu_term (MENU *menu, void (*f)(MENU *))
|   
|   Description   :  Set user-exit which is called when menu is unposted
|                    or just before the top row changes.
|
|   Return Values :  E_OK               - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(menu, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void (*)(MENU *) menu_term(const MENU *menu)
|   
|   Description   :  Return address of user-exit function which is called
|                    when a menu is unposted or just before the top row 
|                    changes.
|
|   Return Values :  Menu finalization function address or NULL
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(menu, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_item_init (MENU *menu, void (*f)(MENU *))
|   
|   Description   :  Set user-exit which is called when menu is posted
|                    or just after the current item changes.
|
|   Return Values :  E_OK               - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(item, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void (*)(MENU *) item_init (const MENU *menu)
|   
|   Description   :  Return address of user-exit function which is called
|                    when a menu is posted or just after the current item 
|                    changes.
|
|   Return Values :  Item init function address or NULL
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(item, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_item_term (MENU *menu, void (*f)(MENU *))
|   
|   Description   :  Set user-exit which is called when menu is unposted
|                    or just before the current item changes.
|
|   Return Values :  E_OK               - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(item, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void (*)(MENU *) item_init (const MENU *menu)
|   
|   Description   :  Return address of user-exit function which is called
|                    when a menu is unposted or just before the current item 
|                    changes.
|
|   Return Values :  Item finalization function address or NULL
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(item, term)

/* m_hook.c ends here */
