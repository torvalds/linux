/****************************************************************************
 * Copyright (c) 1998,2006 Free Software Foundation, Inc.                   *
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

/*
 * $Id: chkdef.cmd,v 1.3 2006/04/22 23:14:50 tom Exp $
 *
 * Author:  Juan Jose Garcia Ripoll <worm@arrakis.es>.
 * Webpage: http://www.arrakis.es/~worm/
 *
 * chkdef.cmd - checks that a .def file has no conflicts and is properly
 *		formatted.
 *
 * returns nonzero if two symbols have the same code or a line has a wrong
 * format.
 *
 * returns 0 otherwise
 *
 * the standard output shows conflicts.
 */
parse arg def_file

def_file = translate(def_file,'\','/')

call CleanQueue

/*
 * `cmp' is zero when the file is valid
 * `codes' associates a name to a code
 * `names' associates a code to a name
 */
cmp    = 0
codes. = 0
names. = ''

/*
 * This sed expression cleans empty lines, comments and special .DEF
 * commands, such as LIBRARY..., EXPORTS..., etc
 */
tidy_up  = '"s/[ 	][ 	]*/ /g;s/;.*//g;/^[ ]*$/d;/^[a-zA-Z]/d;"'

/*
 * First we find all public symbols from the original DLL. All this
 * information is pushed into a REXX private list with the RXQUEUE
 * utility program.
 */
'@echo off'
'type' def_file '| sed' tidy_up '| sort | rxqueue'

do while queued() > 0
   /*
    * We retrieve the symbol name (NEW_NAME) and its code (NEW_CODE)
    */
   parse pull '"' new_name '"' '@'new_code rest
   select
      when (new_code = '') | (new_name = '') then
         /* The input was not properly formatted */
         do
         say 'Error: symbol "'new_name'" has no export code or is empty'
         cmp = 1
         end
      when codes.new_name \= 0 then
         /* This symbol was already defined */
         if codes.new_name \= new_code then
            do
	    cmp = 2
 	    say 'Symbol "'new_name'" multiply defined'
	    end
      when names.new_code \= '' then
         /* This code was already assigned to a symbol */
         if names.new_code \= new_name then
            do
            cmp = 3
	    say 'Conflict with "'names.new_code'" & "'new_name'" being @'new_code
            end
      otherwise
         do
         codes.new_name = new_code
         names.new_code = new_name
         end
   end  /* select */
end

exit cmp

CleanQueue: procedure
	do while queued() > 0
	   parse pull foo
	end
return
