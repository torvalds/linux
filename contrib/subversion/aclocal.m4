#
#
#   Licensed to the Apache Software Foundation (ASF) under one
#   or more contributor license agreements.  See the NOTICE file
#   distributed with this work for additional information
#   regarding copyright ownership.  The ASF licenses this file
#   to you under the Apache License, Version 2.0 (the
#   "License"); you may not use this file except in compliance
#   with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing,
#   software distributed under the License is distributed on an
#   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#   KIND, either express or implied.  See the License for the
#   specific language governing permissions and limitations
#   under the License.
#
#
# aclocal.m4: Supplementary macros used by Subversion's configure.ac
#
# These are here rather than directly in configure.ac, since this prevents
# comments in the macro files being copied into configure.ac, producing
# useless bloat. (This is significant - a 12kB reduction in size!)

# Include macros distributed by the APR project
sinclude(build/ac-macros/find_apr.m4)
sinclude(build/ac-macros/find_apu.m4)

# Include Subversion's own custom macros
sinclude(build/ac-macros/svn-macros.m4)

sinclude(build/ac-macros/apache.m4)
sinclude(build/ac-macros/apr.m4)
sinclude(build/ac-macros/aprutil.m4)
sinclude(build/ac-macros/apr_memcache.m4)
sinclude(build/ac-macros/berkeley-db.m4)
sinclude(build/ac-macros/compiler.m4)
sinclude(build/ac-macros/ctypesgen.m4)
sinclude(build/ac-macros/java.m4)
sinclude(build/ac-macros/sasl.m4)
sinclude(build/ac-macros/serf.m4)
sinclude(build/ac-macros/sqlite.m4)
sinclude(build/ac-macros/swig.m4)
sinclude(build/ac-macros/zlib.m4)
sinclude(build/ac-macros/lz4.m4)
sinclude(build/ac-macros/kwallet.m4)
sinclude(build/ac-macros/libsecret.m4)
sinclude(build/ac-macros/utf8proc.m4)
sinclude(build/ac-macros/macosx.m4)

# Include the libtool macros
sinclude(build/libtool.m4)
sinclude(build/ltoptions.m4)
sinclude(build/ltsugar.m4)
sinclude(build/ltversion.m4)
sinclude(build/lt~obsolete.m4)
