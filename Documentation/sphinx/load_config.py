# -*- coding: utf-8; mode: python -*-
# pylint: disable=R0903, C0330, R0914, R0912, E0401

import os
import sys
from sphinx.util.pycompat import execfile_

# ------------------------------------------------------------------------------
def loadConfig(namespace):
# ------------------------------------------------------------------------------

    u"""Load an additional configuration file into *namespace*.

    The name of the configuration file is taken from the environment
    ``SPHINX_CONF``. The external configuration file extends (or overwrites) the
    configuration values from the origin ``conf.py``.  With this you are able to
    maintain *build themes*.  """

    config_file = os.environ.get("SPHINX_CONF", None)
    if (config_file is not None
        and os.path.normpath(namespace["__file__"]) != os.path.normpath(config_file) ):
        config_file = os.path.abspath(config_file)

        if os.path.isfile(config_file):
            sys.stdout.write("load additional sphinx-config: %s\n" % config_file)
            config = namespace.copy()
            config['__file__'] = config_file
            execfile_(config_file, config)
            del config['__file__']
            namespace.update(config)
        else:
            sys.stderr.write("WARNING: additional sphinx-config not found: %s\n" % config_file)
