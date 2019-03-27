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
 * $Id: makedef.cmd,v 1.5 2006/04/22 23:14:50 tom Exp $
 *
 * Author:  Juan Jose Garcia Ripoll <worm@arrakis.es>.
 * Webpage: http://www.arrakis.es/~worm/
 *
 * makedef.cmd - update a DLL export list using a newly created library file
 *		 in a.out format, plus an old .DEF file.
 *
 * standard output gets a sorted list with all entrypoints with entrycodes.
 * This list, plus a few .def sentences (LIBRARY, DESCRIPTION and EXPORT)
 * is used to build a new .def file.
 *
 * `_nc_*' symbols are ignored.
 *
 * returns 1 when the old def_file is corrupted -- that is, export items are
 * not properly formatted.
 *
 * returns 0 if everything went OK.
 */

parse arg lib_file def_file

lib_file = translate(lib_file,'\','/')
def_file = translate(def_file,'\','/')

call CleanQueue

/*
 * `codes' is the stem that links a code to every symbol
 * `names' is the stem where symbols are stored sequentially
 * `last' is the index of the last symbol defined
 */
last   = 0
used.  = 0
codes. = 0
names. = ''

tmp_name = 'foo.tmp'

/*
 * This sed expression cleans empty lines, comments and special .DEF
 * commands, such as LIBRARY..., EXPORTS..., etc
 */
tidy_up  = '"/^[A-Z]/d;s/[ 	][ 	]*/ /g;s/;.*$//g;s/^[ ]*//g;/^[ ]*$/d"'

/*
 * First we find all public symbols (functions and variables). Next we
 * concatenate this list with the old one, sorting it and wiping out
 * all unused data (comments, DLL directives, blanks, etc). All this
 * information is pushed into a REXX private list with the RXQUEUE
 * utility program.
 */
'@echo off'
'emxexp -u' lib_file '>' tmp_name
'cat' tmp_name def_file '| sed' tidy_up '| sort > foo2.tmp'
'type foo2.tmp | rxqueue'
'del' tmp_name '1>NUL'

/*
 * This loop runs over the queue items
 */
do while queued() > 0
   /*
    * We retrieve the symbol name (NEW_NAME) and its number (NEW_NUMBER)
    * When the line comes from `emximp's output, there's no number, so
    * we assign it the special value 0.
    */
   parse pull new_symbol '@'new_code rest
   if Left(new_symbol,1) = '"' then
      parse var new_symbol '"' new_name '"' rest
   else
      do
      echo 'Symbol 'new_symbol' was not quoted'
      new_name = new_symbol
      end

   if new_code = '' then
      new_code = 0
   /*
    * Here, one would place all smart checks that would kill unused symbols.
    * However, export tables are not that big, so why bothering?
   if Left(new_name,4) = '_nc_' then
      iterate
    */
   /*
    * The algorithm:
    *	IF (this is the 2nd time the symbol appears) THEN
    *		(this symbol comes from a .DEF file)
    *		it has a valid code that we store
    *		we mark that code as used
    *   ELIF (it has no number) THEN
    *		(it's a new symbol)
    *		we increase the counter of defined symbols
    *		we assign it the special number 0
    *		(later on it'll be assigned an unused export code)
    *   ELSE
    *		this symbol was in the old DLL and it's no longer
    *		here, so we skip it.
    */
   select
      when new_name = '' then
         'echo Warning: empty symbol found 1>&2'
      when names.last = new_name then
         do
         codes.last = new_code
         used.new_code = 1
         end
      when new_code = 0 then
         do
         last = last + 1
         names.last = new_name
         codes.last = 0
         end
   otherwise
      'echo Warning: symbol "'new_name'" has disappeared 1>&2'
   end /* select */
end /* do while queued() */

/*
 * Finally we scan the stem, writing out all symbols with export codes.
 * Those that did not have a valid one (just 0) are assigned a new one.
 */
new_code = 1
inx = 1
do while inx <= last
   if codes.inx = 0 then
      do
      do while used.new_code \= 0
         new_code = new_code + 1
      end
      codes.inx = new_code
      used.new_code = 1
      end
   say '	"'names.inx'"	@'codes.inx'	NONAME'
   inx = inx + 1
end
'del foo2.tmp 1>NUL'
exit 0

/*
 * Cleans the REXX queue by pulling and forgetting every line.
 * This is needed, at least, when `makedef.cmd' starts, because an aborted
 * REXX program might have left some rubbish in.
 */
CleanQueue: procedure
   do while queued() > 0
      parse pull foo
   end
return

