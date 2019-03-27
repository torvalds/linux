[+: -*- Mode: nroff -*-

  AutoGen5 template man

## agman-cmd.tpl -- Template for command line man pages
##
##  This file is part of AutoOpts, a companion to AutoGen.
##  AutoOpts is free software.
##  Copyright (C) 1992-2013 Bruce Korb - all rights reserved
##
##  AutoOpts is available under any one of two licenses.  The license
##  in use must be one of these two and the choice is under the control
##  of the user of the license.
##
##   The GNU Lesser General Public License, version 3 or later
##      See the files "COPYING.lgplv3" and "COPYING.gplv3"
##
##   The Modified Berkeley Software Distribution License
##      See the file "COPYING.mbsd"
##
##  These files have the following sha256 sums:
##
##  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
##  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
##  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd

# Produce a man page for section 1, 5 or 8 commands.
# Which is selected via:  -DMAN_SECTION=n
# passed to the autogen invocation.  "n" may have a suffix, if desired.
#
:+][+:

(define head-line (lambda()
        (sprintf ".TH %s %s \"%s\" \"%s\" \"%s\"\n.\\\"\n"
                (get "prog-name") man-sect
        (shell "date '+%d %b %Y'") package-text section-name) ))

(define man-page #t)
(out-push-new)                              :+][+:

INCLUDE "mdoc-synopsis.tlib"                :+][+:
INCLUDE "cmd-doc.tlib"                      :+][+:

INVOKE build-doc                            :+][+:

  (shell (string-append
    "fn='" (find-file "mdoc2man") "'\n"
    "test -f ${fn} || die mdoc2man not found from $PWD\n"
    "${fn} <<\\_EndOfMdoc_ || die ${fn} failed in $PWD\n"
    (out-pop #t)
    "\n_EndOfMdoc_" ))

:+][+:

(out-move (string-append (get "prog-name") "."
          man-sect))      :+][+:

agman-cmd.tpl ends here   :+]
