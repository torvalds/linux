# SPDX-License-Identifier: GPL-2.0
#
# Sphinx has deprecated its older logging interface, but the replacement
# only goes back to 1.6.  So here's a wrapper layer to keep around for
# as long as we support 1.4.
#
# We don't support 1.4 anymore, but we'll keep the wrappers around until
# we change all the code to not use them anymore :)
#
import sphinx
from sphinx.util import logging

logger = logging.getLogger('kerneldoc')

def warn(app, message):
    logger.warning(message)

def verbose(app, message):
    logger.verbose(message)

def info(app, message):
    logger.info(message)
