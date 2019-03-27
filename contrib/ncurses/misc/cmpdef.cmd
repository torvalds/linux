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
 * $Id: cmpdef.cmd,v 1.3 2006/04/22 23:14:50 tom Exp $
 *
 * Author:  Juan Jose Garcia Ripoll <worm@arrakis.es>.
 * Webpage: http://www.arrakis.es/~worm/
 *
 * cmpdef.cmd - compares two .def files, checking whether they have
 *		the same entries with the same export codes.
 *
 * returns 0 if there are no conflicts between the files -- that is,
 * the newer one can replace the older one.
 *
 * returns 1 when either of the files is not properly formatted and
 * when there are conflicts: two symbols having the same export code.
 *
 * the standard output shows a list with newly added symbols, plus
 * replaced symbols and conflicts.
 */
parse arg def_file1 def_file2

def_file1 = translate(def_file1,'\','/')
def_file2 = translate(def_file2,'\','/')

call CleanQueue

/*
 * `cmp' is zero when the last file is valid and upward compatible
 * `numbers' is the stem where symbols are stored
 */
cmp      = 0
names.   = ''
numbers. = 0

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
'type' def_file1 '| sed' tidy_up '| sort | rxqueue'

do while queued() > 0
   /*
    * We retrieve the symbol name (NAME) and its number (NUMBER)
    */
   parse pull '"' name '"' '@'number rest
   if number = '' || name = '' then
      do
      say 'Corrupted file' def_file1
      say 'Symbol' name 'has no number'
      exit 1
      end
   else
      do
      numbers.name = number
      names.number = name
      end
end

/*
 * Now we find all public symbols from the new DLL, and compare.
 */
'type' def_file2 '| sed' tidy_up '| sort | rxqueue'

do while queued() > 0
   parse pull '"' name '"' '@'number rest
   if name = '' | number = '' then
      do
      say 'Corrupted file' def_file2
      say 'Symbol' name 'has no number'
      exit 1
      end
   if numbers.name = 0 then
      do
      cmp = 1
      if names.number = '' then
         say 'New symbol' name 'with code @'number
      else
         say 'Conflict old =' names.number ', new =' name 'at @'number
      end
   else if numbers.name \= number then
      do
      cmp = 1
      say name 'Symbol' name 'changed from @'numbers.name 'to @'number
      end
end /* do */

exit cmp

/*
 * Cleans the REXX queue by pulling and forgetting every line.
 * This is needed, at least, when `cmpdef.cmd' starts, because an aborted
 * REXX program might have left some rubbish in.
 */
CleanQueue: procedure
   do while queued() > 0
      parse pull foo
   end
return

