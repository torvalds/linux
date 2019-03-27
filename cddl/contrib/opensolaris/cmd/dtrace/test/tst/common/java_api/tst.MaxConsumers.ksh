#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

# ident	"%Z%%M%	%I%	%E% SMI"

############################################################################
# ASSERTION:
#	Fixed bug 6506495 -DJAVA_DTRACE_MAX_CONSUMERS=N for any N < 8 is
#	treated as if it were 8.
#
# SECTION: Java API
#
############################################################################

java -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=-1 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=0 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=1 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=2 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=7 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=8 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=9 -cp test.jar TestMaxConsumers
java -DJAVA_DTRACE_MAX_CONSUMERS=19 -cp test.jar TestMaxConsumers
