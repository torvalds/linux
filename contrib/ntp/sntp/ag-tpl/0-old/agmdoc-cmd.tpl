[+: -*- Mode: nroff -*-

  AutoGen5 template mdoc

## agman-cmd.tpl -- Template for command line mdoc pages
##
##  This file is part of AutoOpts, a companion to AutoGen.
##  AutoOpts is free software.
##  AutoOpts is Copyright (C) 1992-2013 by Bruce Korb - all rights reserved
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

(define head-line (lambda() (string-append
   ".Dd "   (shell "date '+%B %e %Y' | sed 's/  */ /g'")
   "\n.Dt " UP-PROG-NAME " " man-sect " " section-name
   "\n.Os\n") ))

(define man-page #f)                        :+][+:

INCLUDE "mdoc-synopsis.tlib"                :+][+:
INCLUDE "cmd-doc.tlib"                      :+][+:
INVOKE build-doc                            :+][+:

(out-move (string-append
          (get "prog-name") "." man-sect))  :+][+:
agmdoc-cmd.tpl ends here  :+]
