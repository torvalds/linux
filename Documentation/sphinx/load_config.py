# -*- coding: utf-8; mode: python -*-
# SPDX-License-Identifier: GPL-2.0
# pylint: disable=R0903, C0330, R0914, R0912, E0401

import os
import sys
from sphinx.util.osutil import fs_encoding

# ------------------------------------------------------------------------------
def loadConfig(namespace):
# ------------------------------------------------------------------------------

    """Load an additional configuration file into *namespace*.

    The name of the configuration file is taken from the environment
    ``SPHINX_CONF``. The external configuration file extends (or overwrites) the
    configuration values from the origin ``conf.py``.  With this you are able to
    maintain *build themes*.  """

    config_file = os.environ.get("SPHINX_CONF", None)
    if (config_file is not None
        and os.path.normpath(namespace["__file__"]) != os.path.normpath(config_file) ):
        config_file = os.path.abspath(config_file)

        # Let's avoid one conf.py file just due to latex_documents
        start = config_file.find('Documentation/')
        if start >= 0:
            start = config_file.find('/', start + 1)

        end = config_file.rfind('/')
        if start >= 0 and end > 0:
            dir = config_file[start + 1:end]

            print("source directory: %s" % dir)
            new_latex_docs = []
            latex_documents = namespace['latex_documents']

            for l in latex_documents:
                if l[0].find(dir + '/') == 0:
                    has = True
                    fn = l[0][len(dir) + 1:]
                    new_latex_docs.append((fn, l[1], l[2], l[3], l[4]))
                    break

            namespace['latex_documents'] = new_latex_docs

        # If there is an extra conf.py file, load it
        if os.path.isfile(config_file):
            sys.stdout.write("load additional sphinx-config: %s\n" % config_file)
            config = namespace.copy()
            config['__file__'] = config_file
            with open(config_file, 'rb') as f:
                code = compile(f.read(), fs_encoding, 'exec')
                exec(code, config)
            del config['__file__']
            namespace.update(config)
        else:
            config = namespace.copy()
            config['tags'].add("subproject")
            namespace.update(config)
