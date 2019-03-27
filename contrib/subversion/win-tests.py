#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
"""
Driver for running the tests on Windows.

For a list of options, run this script with the --help option.
"""

# $HeadURL: https://svn.apache.org/repos/asf/subversion/branches/1.10.x/win-tests.py $
# $LastChangedRevision: 1813897 $

import os, sys, subprocess
import filecmp
import shutil
import traceback
import logging
import re
try:
  # Python >=3.0
  import configparser
except ImportError:
  # Python <3.0
  import ConfigParser as configparser
import string
import random

import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt

def _usage_exit():
  "print usage, exit the script"

  print("Driver for running the tests on Windows.")
  print("Usage: python win-tests.py [option] [test-path]")
  print("")
  print("Valid options:")
  print("  -r, --release          : test the Release configuration")
  print("  -d, --debug            : test the Debug configuration (default)")
  print("  --bin=PATH             : use the svn binaries installed in PATH")
  print("  -u URL, --url=URL      : run ra_dav or ra_svn tests against URL;")
  print("                           will start svnserve for ra_svn tests")
  print("  -v, --verbose          : talk more")
  print("  -f, --fs-type=type     : filesystem type to use (fsfs is default)")
  print("  -c, --cleanup          : cleanup after running a test")
  print("  -t, --test=TEST        : Run the TEST test (all is default); use")
  print("                           TEST#n to run a particular test number,")
  print("                           multiples also accepted e.g. '2,4-7'")
  print("  --log-level=LEVEL      : Set log level to LEVEL (E.g. DEBUG)")
  print("  --log-to-stdout        : Write log results to stdout")

  print("  --svnserve-args=list   : comma-separated list of arguments for")
  print("                           svnserve")
  print("                           default is '-d,-r,<test-path-root>'")
  print("  --asp.net-hack         : use '_svn' instead of '.svn' for the admin")
  print("                           dir name")
  print("  --httpd-dir            : location where Apache HTTPD is installed")
  print("  --httpd-port           : port for Apache HTTPD; random port number")
  print("                           will be used, if not specified")
  print("  --httpd-daemon         : Run Apache httpd as daemon")
  print("  --httpd-service        : Run Apache httpd as Windows service (default)")
  print("  --httpd-no-log         : Disable httpd logging")
  print("  --http-short-circuit   : Use SVNPathAuthz short_circuit on HTTP server")
  print("  --disable-http-v2      : Do not advertise support for HTTPv2 on server")
  print("  --disable-bulk-updates : Disable bulk updates on HTTP server")
  print("  --ssl-cert             : Path to SSL server certificate to trust.")
  print("  --https                : Run Apache httpd with an https setup.")
  print("  --http2                : Enable http2 in Apache Httpd (>= 2.4.17).")
  print("  --mod-deflate          : Enable mod_deflate in Apache Httpd.")
  print("  --global-scheduler     : Enable global scheduler.")
  print("  --exclusive-wc-locks   : Enable exclusive working copy locks")
  print("  --memcached-dir=DIR    : Run memcached from dir")
  print("  --memcached-server=    : Enable usage of the specified memcached server")
  print("              <url:port>")
  print("  --skip-c-tests         : Skip all C tests")
  print("  --dump-load-cross-check: Run the dump load cross check after every test")

  print("  --javahl               : Run the javahl tests instead of the normal tests")
  print("  --swig=language        : Run the swig perl/python/ruby tests instead of")
  print("                           the normal tests")
  print("  --list                 : print test doc strings only")
  print("  --milestone-filter=RE  : RE is a regular expression pattern that (when")
  print("                           used with --list) limits the tests listed to")
  print("                           those with an associated issue in the tracker")
  print("                           which has a target milestone that matches RE.")
  print("  --mode-filter=TYPE     : limit tests to expected TYPE = XFAIL, SKIP, PASS,")
  print("                           or 'ALL' (default)")
  print("  --enable-sasl          : enable Cyrus SASL authentication for")
  print("                           svnserve")
  print("  -p, --parallel         : run multiple tests in parallel")
  print("  --server-minor-version : the minor version of the server being")
  print("                           tested")
  print("  --config-file          : Configuration file for tests")
  print("  --fsfs-sharding        : Specify shard size (for fsfs)")
  print("  --fsfs-packing         : Run 'svnadmin pack' automatically")
  print("  --fsfs-compression=VAL : Set compression type to VAL (for fsfs)")
  print("  -q, --quiet            : Deprecated; this is the default.")
  print("                           Use --set-log-level instead.")

  sys.exit(0)

CMDLINE_TEST_SCRIPT_PATH = 'subversion/tests/cmdline/'
CMDLINE_TEST_SCRIPT_NATIVE_PATH = CMDLINE_TEST_SCRIPT_PATH.replace('/', os.sep)

sys.path.insert(0, os.path.join('build', 'generator'))
sys.path.insert(1, 'build')

import gen_win_dependencies
import gen_base
version_header = os.path.join('subversion', 'include', 'svn_version.h')
cp = configparser.ConfigParser()
cp.read('gen-make.opts')
gen_obj = gen_win_dependencies.GenDependenciesBase('build.conf', version_header,
                                                   cp.items('options'))
opts, args = my_getopt(sys.argv[1:], 'hrdvqct:pu:f:',
                       ['release', 'debug', 'verbose', 'quiet', 'cleanup',
                        'test=', 'url=', 'svnserve-args=', 'fs-type=', 'asp.net-hack',
                        'httpd-dir=', 'httpd-port=', 'httpd-daemon', 'https',
                        'httpd-server', 'http-short-circuit', 'httpd-no-log',
                        'disable-http-v2', 'disable-bulk-updates', 'help',
                        'fsfs-packing', 'fsfs-sharding=', 'javahl', 'swig=',
                        'list', 'enable-sasl', 'bin=', 'parallel', 'http2',
                        'mod-deflate', 'global-scheduler',
                        'config-file=', 'server-minor-version=', 'log-level=',
                        'log-to-stdout', 'mode-filter=', 'milestone-filter=',
                        'ssl-cert=', 'exclusive-wc-locks', 'memcached-server=',
                        'skip-c-tests', 'dump-load-cross-check', 'memcached-dir=',
                        'fsfs-compression=',
                        ])
if len(args) > 1:
  print('Warning: non-option arguments after the first one will be ignored')

# Interpret the options and set parameters
base_url, fs_type, verbose, cleanup = None, None, None, None
global_scheduler = None
repo_loc = 'local repository.'
objdir = 'Debug'
log = 'tests.log'
faillog = 'fails.log'
run_svnserve = None
svnserve_args = None
run_httpd = None
httpd_port = None
httpd_service = None
httpd_no_log = None
use_ssl = False
use_http2 = False
use_mod_deflate = False
http_short_circuit = False
advertise_httpv2 = True
http_bulk_updates = True
list_tests = None
milestone_filter = None
test_javahl = None
test_swig = None
enable_sasl = None
svn_bin = None
parallel = None
fsfs_sharding = None
fsfs_packing = None
server_minor_version = None
config_file = None
log_to_stdout = None
mode_filter=None
tests_to_run = []
log_level = None
ssl_cert = None
exclusive_wc_locks = None
run_memcached = None
memcached_server = None
memcached_dir = None
skip_c_tests = None
dump_load_cross_check = None
fsfs_compression = None
fsfs_dir_deltification = None

for opt, val in opts:
  if opt in ('-h', '--help'):
    _usage_exit()
  elif opt in ('-u', '--url'):
    base_url = val
  elif opt in ('-f', '--fs-type'):
    fs_type = val
  elif opt in ('-v', '--verbose'):
    verbose = 1
    log_level = logging.DEBUG
  elif opt in ('-c', '--cleanup'):
    cleanup = 1
  elif opt in ('-t', '--test'):
    tests_to_run.append(val)
  elif opt in ['-r', '--release']:
    objdir = 'Release'
  elif opt in ['-d', '--debug']:
    objdir = 'Debug'
  elif opt == '--svnserve-args':
    svnserve_args = val.split(',')
    run_svnserve = 1
  elif opt == '--asp.net-hack':
    os.environ['SVN_ASP_DOT_NET_HACK'] = opt
  elif opt == '--httpd-dir':
    abs_httpd_dir = os.path.abspath(val)
    run_httpd = 1
  elif opt == '--httpd-port':
    httpd_port = int(val)
  elif opt == '--httpd-daemon':
    httpd_service = 0
  elif opt == '--httpd-service':
    httpd_service = 1
  elif opt == '--httpd-no-log':
    httpd_no_log = 1
  elif opt == '--https':
    use_ssl = 1
  elif opt == '--http2':
    use_http2 = 1
  elif opt == '--mod-deflate':
    use_mod_deflate = 1
  elif opt == '--http-short-circuit':
    http_short_circuit = True
  elif opt == '--disable-http-v2':
    advertise_httpv2 = False
  elif opt == '--disable-bulk-updates':
    http_bulk_updates = False
  elif opt == '--fsfs-sharding':
    fsfs_sharding = int(val)
  elif opt == '--fsfs-packing':
    fsfs_packing = 1
  elif opt == '--javahl':
    test_javahl = 1
  elif opt == '--global-scheduler':
    global_scheduler = 1
  elif opt == '--swig':
    if val not in ['perl', 'python', 'ruby']:
      sys.stderr.write('Running \'%s\' swig tests not supported (yet).\n'
                        % (val,))
    test_swig = val
  elif opt == '--list':
    list_tests = 1
  elif opt == '--milestone-filter':
    milestone_filter = val
  elif opt == '--mode-filter':
    mode_filter = val
  elif opt == '--enable-sasl':
    enable_sasl = 1
    base_url = "svn://localhost/"
  elif opt == '--server-minor-version':
    server_minor_version = int(val)
  elif opt == '--bin':
    svn_bin = val
  elif opt in ('-p', '--parallel'):
    parallel = 1
  elif opt in ('--config-file'):
    config_file = val
  elif opt == '--log-to-stdout':
    log_to_stdout = 1
  elif opt == '--log-level':
    log_level = getattr(logging, val, None) or int(val)
  elif opt == '--ssl-cert':
    ssl_cert = val
  elif opt == '--exclusive-wc-locks':
    exclusive_wc_locks = 1
  elif opt == '--memcached-server':
    memcached_server = val
  elif opt == '--skip-c-tests':
    skip_c_tests = 1
  elif opt == '--dump-load-cross-check':
    dump_load_cross_check = 1
  elif opt == '--memcached-dir':
    memcached_dir = val
    run_memcached = 1
  elif opt == '--fsfs-compression':
    fsfs_compression = val
  elif opt == '--fsfs-dir-deltification':
    fsfs_dir_deltification = val

# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
abs_objdir = os.path.join(abs_srcdir, objdir)
if len(args) == 0:
  abs_builddir = abs_objdir
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  create_dirs = 1

# Default to fsfs explicitly
if not fs_type:
  fs_type = 'fsfs'

if fs_type == 'bdb':
  all_tests = gen_obj.test_progs + gen_obj.bdb_test_progs \
            + gen_obj.scripts + gen_obj.bdb_scripts
else:
  all_tests = gen_obj.test_progs + gen_obj.scripts

client_tests = [x for x in all_tests if x.startswith(CMDLINE_TEST_SCRIPT_PATH)]

if run_httpd:
  if not httpd_port:
    httpd_port = random.randrange(1024, 30000)
  if not base_url:
    if use_ssl:
      scheme = 'https'
    else:
      scheme = 'http'

    base_url = '%s://localhost:%d' % (scheme, httpd_port)

if base_url:
  repo_loc = 'remote repository ' + base_url + '.'
  if base_url[:4] == 'http':
    log = 'dav-tests.log'
    faillog = 'dav-fails.log'
  elif base_url[:3] == 'svn':
    log = 'svn-tests.log'
    faillog = 'svn-fails.log'
    run_svnserve = 1
  else:
    # Don't know this scheme, but who're we to judge whether it's
    # correct or not?
    log = 'url-tests.log'
    faillog = 'url-fails.log'

# Have to move the executables where the tests expect them to be
copied_execs = []   # Store copied exec files to avoid the final dir scan

def create_target_dir(dirname):
  tgt_dir = os.path.join(abs_builddir, dirname)
  if not os.path.exists(tgt_dir):
    if verbose:
      print("mkdir: %s" % tgt_dir)
    os.makedirs(tgt_dir)

def copy_changed_file(src, tgt=None, to_dir=None, cleanup=True):
  if not os.path.isfile(src):
    print('Could not find ' + src)
    sys.exit(1)

  if to_dir and not tgt:
    tgt = os.path.join(to_dir, os.path.basename(src))
  elif not tgt or (tgt and to_dir):
    raise RuntimeError("Using 'tgt' *or* 'to_dir' is required" % (tgt,))
  elif tgt and os.path.isdir(tgt):
    raise RuntimeError("'%s' is a directory. Use to_dir=" % (tgt,))

  if os.path.exists(tgt):
    assert os.path.isfile(tgt)
    if filecmp.cmp(src, tgt):
      if verbose:
        print("same: %s" % src)
        print(" and: %s" % tgt)
      return 0
  if verbose:
    print("copy: %s" % src)
    print("  to: %s" % tgt)
  shutil.copy(src, tgt)

  if cleanup:
    copied_execs.append(tgt)

def locate_libs():
  "Move DLLs to a known location and set env vars"

  debug = (objdir == 'Debug')

  for lib in gen_obj._libraries.values():

    if debug:
      name, dir = lib.debug_dll_name, lib.debug_dll_dir
    else:
      name, dir = lib.dll_name, lib.dll_dir

    if name and dir:
      src = os.path.join(dir, name)
      if os.path.exists(src):
        copy_changed_file(src, to_dir=abs_builddir, cleanup=False)

    for name in lib.extra_bin:
      src = os.path.join(dir, name)
      copy_changed_file(src, to_dir=abs_builddir)


  # Copy the Subversion library DLLs
  for i in gen_obj.graph.get_all_sources(gen_base.DT_INSTALL):
    if isinstance(i, gen_base.TargetLib) and i.msvc_export:
      src = os.path.join(abs_objdir, i.filename)
      if os.path.isfile(src):
        copy_changed_file(src, to_dir=abs_builddir,
                          cleanup=False)

  # Copy the Apache modules
  if run_httpd and cp.has_option('options', '--with-httpd'):
    mod_dav_svn_path = os.path.join(abs_objdir, 'subversion',
                                    'mod_dav_svn', 'mod_dav_svn.so')
    mod_authz_svn_path = os.path.join(abs_objdir, 'subversion',
                                      'mod_authz_svn', 'mod_authz_svn.so')
    mod_dontdothat_path = os.path.join(abs_objdir, 'tools', 'server-side',
                                        'mod_dontdothat', 'mod_dontdothat.so')

    copy_changed_file(mod_dav_svn_path, to_dir=abs_builddir, cleanup=False)
    copy_changed_file(mod_authz_svn_path, to_dir=abs_builddir, cleanup=False)
    copy_changed_file(mod_dontdothat_path, to_dir=abs_builddir, cleanup=False)

  os.environ['PATH'] = abs_builddir + os.pathsep + os.environ['PATH']

def fix_case(path):
    path = os.path.normpath(path)
    parts = path.split(os.path.sep)
    drive = parts[0].upper()
    parts = parts[1:]
    path = drive + os.path.sep
    for part in parts:
        dirs = os.listdir(path)
        for dir in dirs:
            if dir.lower() == part.lower():
                path = os.path.join(path, dir)
                break
    return path

class Svnserve:
  "Run svnserve for ra_svn tests"
  def __init__(self, svnserve_args, objdir, abs_objdir, abs_builddir):
    self.args = svnserve_args
    self.name = 'svnserve.exe'
    self.kind = objdir
    self.path = os.path.join(abs_objdir,
                             'subversion', 'svnserve', self.name)
    self.root = os.path.join(abs_builddir, CMDLINE_TEST_SCRIPT_NATIVE_PATH)
    self.proc = None

  def __del__(self):
    "Stop svnserve when the object is deleted"
    self.stop()

  def _quote(self, arg):
    if ' ' in arg:
      return '"' + arg + '"'
    else:
      return arg

  def start(self):
    if not self.args:
      args = [self.name, '-d', '-r', self.root]
    else:
      args = [self.name] + self.args
    print('Starting %s %s' % (self.kind, self.name))

    env = os.environ.copy()
    env['SVN_DBG_STACKTRACES_TO_STDERR'] = 'y'
    self.proc = subprocess.Popen([self.path] + args[1:], env=env)

  def stop(self):
    if self.proc is not None:
      try:
        print('Stopping %s' % self.name)
        self.proc.poll();
        if self.proc.returncode is None:
          self.proc.kill();
        return
      except AttributeError:
        pass
    print('Svnserve.stop not implemented')

class Httpd:
  "Run httpd for DAV tests"
  def __init__(self, abs_httpd_dir, abs_objdir, abs_builddir, abs_srcdir,
               httpd_port, service, use_ssl, use_http2, use_mod_deflate,
               no_log, httpv2, short_circuit, bulk_updates):
    self.name = 'apache.exe'
    self.httpd_port = httpd_port
    self.httpd_dir = abs_httpd_dir

    if httpv2:
      self.httpv2_option = 'on'
    else:
      self.httpv2_option = 'off'

    if bulk_updates:
      self.bulkupdates_option = 'on'
    else:
      self.bulkupdates_option = 'off'

    self.service = service
    self.proc = None
    self.path = os.path.join(self.httpd_dir, 'bin', self.name)

    if short_circuit:
      self.path_authz_option = 'short_circuit'
    else:
      self.path_authz_option = 'on'

    if not os.path.exists(self.path):
      self.name = 'httpd.exe'
      self.path = os.path.join(self.httpd_dir, 'bin', self.name)
      if not os.path.exists(self.path):
        raise RuntimeError("Could not find a valid httpd binary!")

    self.root_dir = os.path.join(CMDLINE_TEST_SCRIPT_NATIVE_PATH, 'httpd')
    self.root = os.path.join(abs_builddir, self.root_dir)
    self.authz_file = os.path.join(abs_builddir,
                                   CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                                   'svn-test-work', 'authz')
    self.dontdothat_file = os.path.join(abs_builddir,
                                         CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                                         'svn-test-work', 'dontdothat')
    self.certfile = os.path.join(abs_builddir,
                                 CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                                 'svn-test-work', 'cert.pem')
    self.certkeyfile = os.path.join(abs_builddir,
                                     CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                                     'svn-test-work', 'cert-key.pem')
    self.httpd_config = os.path.join(self.root, 'httpd.conf')
    self.httpd_users = os.path.join(self.root, 'users')
    self.httpd_mime_types = os.path.join(self.root, 'mime.types')
    self.httpd_groups = os.path.join(self.root, 'groups')
    self.abs_builddir = abs_builddir
    self.abs_objdir = abs_objdir
    self.abs_srcdir = abs_srcdir
    self.service_name = 'svn-test-httpd-' + str(httpd_port)

    if self.service:
      self.httpd_args = [self.name, '-n', self._quote(self.service_name),
                         '-f', self._quote(self.httpd_config)]
    else:
      self.httpd_args = [self.name, '-f', self._quote(self.httpd_config)]

    create_target_dir(self.root_dir)

    self._create_users_file()
    self._create_groups_file()
    self._create_mime_types_file()
    self._create_dontdothat_file()

    if use_ssl:
      self._create_cert_files()

    # Obtain version.
    version_vals = gen_obj._libraries['httpd'].version.split('.')
    self.httpd_ver = float('%s.%s' % (version_vals[0], version_vals[1]))

    # Create httpd config file
    fp = open(self.httpd_config, 'w')

    # Limit the number of threads (default = 64)
    if not use_http2:
      fp.write('<IfModule mpm_winnt.c>\n')
      fp.write('ThreadsPerChild 16\n')
      fp.write('</IfModule>\n')

    # Global Environment
    fp.write('ServerRoot   ' + self._quote(self.root) + '\n')
    fp.write('DocumentRoot ' + self._quote(self.root) + '\n')
    fp.write('ServerName   localhost\n')
    fp.write('PidFile      pid\n')
    fp.write('ErrorLog     log\n')
    fp.write('Listen       ' + str(self.httpd_port) + '\n')

    if not no_log:
      fp.write('LogFormat    "%h %l %u %t \\"%r\\" %>s %b" common\n')
      fp.write('Customlog    log common\n')
      fp.write('LogLevel     Debug\n')
    else:
      fp.write('LogLevel     Crit\n')

    # Write LoadModule for minimal system module
    if use_ssl:
      fp.write(self._sys_module('ssl_module', 'mod_ssl.so'))
    if use_http2:
      fp.write(self._sys_module('http2_module', 'mod_http2.so'))
    if use_mod_deflate:
      fp.write(self._sys_module('deflate_module', 'mod_deflate.so'))
    fp.write(self._sys_module('dav_module', 'mod_dav.so'))
    if self.httpd_ver >= 2.3:
      fp.write(self._sys_module('access_compat_module', 'mod_access_compat.so'))
      fp.write(self._sys_module('authz_core_module', 'mod_authz_core.so'))
      fp.write(self._sys_module('authz_user_module', 'mod_authz_user.so'))
      fp.write(self._sys_module('authn_core_module', 'mod_authn_core.so'))
    if self.httpd_ver >= 2.2:
      fp.write(self._sys_module('auth_basic_module', 'mod_auth_basic.so'))
      fp.write(self._sys_module('authn_file_module', 'mod_authn_file.so'))
      fp.write(self._sys_module('authz_groupfile_module', 'mod_authz_groupfile.so'))
      fp.write(self._sys_module('authz_host_module', 'mod_authz_host.so'))
    else:
      fp.write(self._sys_module('auth_module', 'mod_auth.so'))
    fp.write(self._sys_module('alias_module', 'mod_alias.so'))
    fp.write(self._sys_module('mime_module', 'mod_mime.so'))
    fp.write(self._sys_module('log_config_module', 'mod_log_config.so'))

    # Write LoadModule for Subversion modules
    fp.write(self._svn_module('dav_svn_module', 'mod_dav_svn.so'))
    fp.write(self._svn_module('authz_svn_module', 'mod_authz_svn.so'))

    # And for mod_dontdothat
    fp.write(self._svn_module('dontdothat_module', 'mod_dontdothat.so'))

    if use_ssl:
      fp.write('SSLEngine on\n')
      fp.write('SSLProtocol All -SSLv2 -SSLv3\n')
      fp.write('SSLCertificateFile %s\n' % self._quote(self.certfile))
      fp.write('SSLCertificateKeyFile %s\n' % self._quote(self.certkeyfile))

    if use_ssl and use_http2:
      fp.write('Protocols h2 http/1.1\n')
    elif use_http2:
      fp.write('Protocols h2c http/1.1\n')
      fp.write('H2Direct on\n')

    if use_mod_deflate:
      fp.write('SetOutputFilter DEFLATE\n')

    # Don't handle .htaccess, symlinks, etc.
    fp.write('<Directory />\n')
    fp.write('AllowOverride None\n')
    fp.write('Options None\n')
    fp.write('</Directory>\n\n')

    # Define two locations for repositories
    fp.write(self._svn_repo('repositories'))
    fp.write(self._svn_repo('local_tmp'))
    fp.write(self._svn_authz_repo())

    # And two redirects for the redirect tests
    fp.write('RedirectMatch permanent ^/svn-test-work/repositories/'
             'REDIRECT-PERM-(.*)$ /svn-test-work/repositories/$1\n')
    fp.write('RedirectMatch           ^/svn-test-work/repositories/'
             'REDIRECT-TEMP-(.*)$ /svn-test-work/repositories/$1\n')

    fp.write('TypesConfig     ' + self._quote(self.httpd_mime_types) + '\n')
    fp.write('HostNameLookups Off\n')

    fp.close()

  def __del__(self):
    "Stop httpd when the object is deleted"
    self.stop()

  def _quote(self, arg):
    if ' ' in arg:
      return '"' + arg + '"'
    else:
      return arg

  def _create_users_file(self):
    "Create users file"
    htpasswd = os.path.join(self.httpd_dir, 'bin', 'htpasswd.exe')
    # Create the cheapest to compare password form for our testsuite
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-bcp', self.httpd_users,
                                    'jrandom', 'rayjandom'])
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-bp',  self.httpd_users,
                                    'jconstant', 'rayjandom'])
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-bp',  self.httpd_users,
                                    '__dumpster__', '__loadster__'])
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-bp',  self.httpd_users,
                                    'JRANDOM', 'rayjandom'])
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-bp',  self.httpd_users,
                                    'JCONSTANT', 'rayjandom'])

  def _create_groups_file(self):
    "Create groups for mod_authz_svn tests"
    fp = open(self.httpd_groups, 'w')
    fp.write('random: jrandom\n')
    fp.write('constant: jconstant\n')
    fp.close()

  def _create_mime_types_file(self):
    "Create empty mime.types file"
    fp = open(self.httpd_mime_types, 'w')
    fp.close()

  def _create_dontdothat_file(self):
    "Create empty mime.types file"
    # If the tests have not previously been run or were cleaned
    # up, then 'svn-test-work' does not exist yet.
    parent_dir = os.path.dirname(self.dontdothat_file)
    if not os.path.exists(parent_dir):
      os.makedirs(parent_dir)

    fp = open(self.dontdothat_file, 'w')
    fp.write('[recursive-actions]\n')
    fp.write('/ = deny\n')
    fp.close()

  def _create_cert_files(self):
    "Create certificate files"
    # The unix build uses certificates encoded in davautocheck.sh
    # Let's just read them from there

    sh_path = os.path.join(self.abs_srcdir, 'subversion', 'tests', 'cmdline',
                           'davautocheck.sh')
    sh = open(sh_path).readlines()

    def cert_extract(lines, what):
      r = []
      pattern = r'cat\s*\>\s*' + re.escape(what) + r'\s*\<\<([A-Z_a-z0-9]+)'
      exit_marker = None
      for i in lines:
        if exit_marker:
          if i.startswith(exit_marker):
            return r
          r.append(i)
        else:
          m = re.match(pattern, i)
          if m:
            exit_marker = m.groups(1)

    cert_file = cert_extract(sh, '"$SSL_CERTIFICATE_FILE"')
    cert_key = cert_extract(sh, '"$SSL_CERTIFICATE_KEY_FILE"')
    open(self.certfile, 'w').write(''.join(cert_file))
    open(self.certkeyfile, 'w').write(''.join(cert_key))

  def _sys_module(self, name, path):
    full_path = os.path.join(self.httpd_dir, 'modules', path)
    return 'LoadModule ' + name + " " + self._quote(full_path) + '\n'

  def _svn_module(self, name, path):
    full_path = os.path.join(self.abs_builddir, path)
    return 'LoadModule ' + name + ' ' + self._quote(full_path) + '\n'

  def _svn_repo(self, name):
    path = os.path.join(self.abs_builddir,
                        CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                        'svn-test-work', name)
    location = '/svn-test-work/' + name
    ddt_location = '/ddt-test-work/' + name
    return \
      '<Location ' + location + '>\n' \
      '  DAV             svn\n' \
      '  SVNParentPath   ' + self._quote(path) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '  SVNAllowBulkUpdates ' + self.bulkupdates_option + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  AuthType        Basic\n' \
      '  AuthName        "Subversion Repository"\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require         valid-user\n' \
      '</Location>\n' \
      '<Location ' + ddt_location + '>\n' \
      '  DAV             svn\n' \
      '  SVNParentPath   ' + self._quote(path) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '  SVNAllowBulkUpdates ' + self.bulkupdates_option + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  AuthType        Basic\n' \
      '  AuthName        "Subversion Repository"\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require         valid-user\n' \
      '  DontDoThatConfigFile ' + self._quote(self.dontdothat_file) + '\n' \
      '</Location>\n'

  def _svn_authz_repo(self):
    local_tmp = os.path.join(self.abs_builddir,
                             CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                             'svn-test-work', 'local_tmp')
    return \
      '<Location /authz-test-work/anon>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  <IfModule mod_authz_core.c>' + '\n' \
      '    Require all granted' + '\n' \
      '  </IfModule>' + '\n' \
      '  <IfModule !mod_authz_core.c>' + '\n' \
      '    Allow from all' + '\n' \
      '  </IfModule>' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/mixed>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  Satisfy Any' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/mixed-noauthwhenanon>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  AuthzSVNNoAuthWhenAnonymousAllowed On' + '\n' \
      '  SVNPathAuthz On' + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/authn>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/authn-anonoff>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  AuthzSVNAnonymous Off' + '\n' \
      '  SVNPathAuthz On' + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/authn-lcuser>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  AuthzForceUsernameCase Lower' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/authn-lcuser>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require           valid-user' + '\n' \
      '  AuthzForceUsernameCase Lower' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/authn-group>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  AuthGroupFile    ' + self._quote(self.httpd_groups) + '\n' \
      '  Require           group random' + '\n' \
      '  AuthzSVNAuthoritative Off' + '\n' \
      '  SVNPathAuthz On' + '\n' \
      '</Location>' + '\n' \
      '<IfModule mod_authz_core.c>' + '\n' \
      '<Location /authz-test-work/sallrany>' + '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  AuthzSendForbiddenOnFailure On' + '\n' \
      '  Satisfy All' + '\n' \
      '  <RequireAny>' + '\n' \
      '    Require valid-user' + '\n' \
      '    Require expr req(\'ALLOW\') == \'1\'' + '\n' \
      '  </RequireAny>' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '<Location /authz-test-work/sallrall>'+ '\n' \
      '  DAV               svn' + '\n' \
      '  SVNParentPath     ' + local_tmp + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  SVNAdvertiseV2Protocol ' + self.httpv2_option + '\n' \
      '  SVNListParentPath On' + '\n' \
      '  AuthType          Basic' + '\n' \
      '  AuthName          "Subversion Repository"' + '\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  AuthzSendForbiddenOnFailure On' + '\n' \
      '  Satisfy All' + '\n' \
      '  <RequireAll>' + '\n' \
      '    Require valid-user' + '\n' \
      '    Require expr req(\'ALLOW\') == \'1\'' + '\n' \
      '  </RequireAll>' + '\n' \
      '  SVNPathAuthz ' + self.path_authz_option + '\n' \
      '</Location>' + '\n' \
      '</IfModule>' + '\n' \

  def start(self):
    if self.service:
      self._start_service()
    else:
      self._start_daemon()

    # Avoid output from starting and preparing between test results
    sys.stderr.flush()
    sys.stdout.flush()

  def stop(self):
    if self.service:
      self._stop_service()
    else:
      self._stop_daemon()

  def _start_service(self):
    "Install and start HTTPD service"
    print('Installing service %s' % self.service_name)
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'install'])
    print('Starting service %s' % self.service_name)
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'start'])

  def _stop_service(self):
    "Stop and uninstall HTTPD service"
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'stop'])
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'uninstall'])

  def _start_daemon(self):
    "Start HTTPD as daemon"
    print('Starting httpd as daemon')
    print(self.httpd_args)
    self.proc = subprocess.Popen([self.path] + self.httpd_args[1:])

  def _stop_daemon(self):
    "Stop the HTTPD daemon"
    if self.proc is not None:
      try:
        print('Stopping %s' % self.name)
        self.proc.poll();
        if self.proc.returncode is None:
          self.proc.kill();
        return
      except AttributeError:
        pass
    print('Httpd.stop_daemon not implemented')

class Memcached:
  "Run memcached for tests"
  def __init__(self, abs_memcached_dir, memcached_server):
    self.name = 'memcached.exe'

    self.memcached_host, self.memcached_port = memcached_server.split(':')
    self.memcached_dir = abs_memcached_dir

    self.proc = None
    self.path = os.path.join(self.memcached_dir, self.name)

    self.memcached_args = [
                            self.name,
                            '-p', self.memcached_port,
                            '-l', self.memcached_host
                          ]

  def __del__(self):
    "Stop memcached when the object is deleted"
    self.stop()

  def start(self):
    "Start memcached as daemon"
    print('Starting %s as daemon' % self.name)
    print(self.memcached_args)
    self.proc = subprocess.Popen([self.path] + self.memcached_args)

  def stop(self):
    "Stop memcached"
    if self.proc is not None:
      try:
        print('Stopping %s' % self.name)
        self.proc.poll();
        if self.proc.returncode is None:
          self.proc.kill();
        return
      except AttributeError:
        pass

# Move the binaries to the test directory
create_target_dir(abs_builddir)
locate_libs()
if create_dirs:
  for i in gen_obj.graph.get_all_sources(gen_base.DT_INSTALL):
    if isinstance(i, gen_base.TargetExe):
      src = os.path.join(abs_objdir, i.filename)

      if os.path.isfile(src):
        dst = os.path.join(abs_builddir, i.filename)
        create_target_dir(os.path.dirname(dst))
        copy_changed_file(src, dst)

# Create the base directory for Python tests
create_target_dir(CMDLINE_TEST_SCRIPT_NATIVE_PATH)

# Ensure the tests directory is correctly cased
abs_builddir = fix_case(abs_builddir)

failed = None
daemon = None
memcached = None
# Run the tests

# No need to start any servers if we are only listing the tests.
if not list_tests:
  if run_memcached:
    memcached = Memcached(memcached_dir, memcached_server)
    memcached.start()

  if run_svnserve:
    daemon = Svnserve(svnserve_args, objdir, abs_objdir, abs_builddir)

  if run_httpd:
    daemon = Httpd(abs_httpd_dir, abs_objdir, abs_builddir, abs_srcdir,
                   httpd_port, httpd_service, use_ssl, use_http2,
                   use_mod_deflate, httpd_no_log, advertise_httpv2,
                   http_short_circuit, http_bulk_updates)

    if use_ssl and not ssl_cert:
      ssl_cert = daemon.certfile

  # Start service daemon, if any
  if daemon:
    daemon.start()

# Find the full path and filename of any test that is specified just by
# its base name.
if len(tests_to_run) != 0:
  tests = []
  for t in tests_to_run:
    tns = None
    if '#' in t:
      t, tns = t.split('#')

    test = [x for x in all_tests if x.split('/')[-1] == t]
    if not test and not (t.endswith('-test.exe') or t.endswith('_tests.py')):
      # The lengths of '-test.exe' and of '_tests.py' are both 9.
      test = [x for x in all_tests if x.split('/')[-1][:-9] == t]

    if not test:
      print("Skipping test '%s', test not found." % t)
    elif tns:
      tests.append('%s#%s' % (test[0], tns))
    else:
      tests.extend(test)

  tests_to_run = tests
else:
  tests_to_run = all_tests


if list_tests:
  print('Listing %s configuration on %s' % (objdir, repo_loc))
else:
  print('Testing %s configuration on %s' % (objdir, repo_loc))
sys.path.insert(0, os.path.join(abs_srcdir, 'build'))

if not test_javahl and not test_swig:
  import run_tests
  if log_to_stdout:
    log_file = None
    fail_log_file = None
  else:
    log_file = os.path.join(abs_builddir, log)
    fail_log_file = os.path.join(abs_builddir, faillog)

  if run_httpd:
    httpd_version = gen_obj._libraries['httpd'].version
  else:
    httpd_version = None

  opts, args = run_tests.create_parser().parse_args([])
  opts.url = base_url
  opts.fs_type = fs_type
  opts.global_scheduler = global_scheduler
  opts.http_library = 'serf'
  opts.server_minor_version = server_minor_version
  opts.cleanup = cleanup
  opts.enable_sasl = enable_sasl
  opts.parallel = parallel
  opts.config_file = config_file
  opts.fsfs_sharding = fsfs_sharding
  opts.fsfs_packing = fsfs_packing
  opts.list_tests = list_tests
  opts.svn_bin = svn_bin
  opts.mode_filter = mode_filter
  opts.milestone_filter = milestone_filter
  opts.httpd_version = httpd_version
  opts.set_log_level = log_level
  opts.ssl_cert = ssl_cert
  opts.exclusive_wc_locks = exclusive_wc_locks
  opts.memcached_server = memcached_server
  opts.skip_c_tests = skip_c_tests
  opts.dump_load_cross_check = dump_load_cross_check
  opts.fsfs_compression = fsfs_compression
  opts.fsfs_dir_deltification = fsfs_dir_deltification
  th = run_tests.TestHarness(abs_srcdir, abs_builddir,
                             log_file, fail_log_file, opts)
  old_cwd = os.getcwd()
  try:
    os.chdir(abs_builddir)
    failed = th.run(tests_to_run)
  except:
    os.chdir(old_cwd)
    raise
  else:
    os.chdir(old_cwd)
elif test_javahl:
  failed = False

  java_exe = None

  for path in os.environ["PATH"].split(os.pathsep):
    if os.path.isfile(os.path.join(path, 'java.exe')):
      java_exe = os.path.join(path, 'java.exe')
      break

  if not java_exe and 'java_sdk' in gen_obj._libraries:
    jdk = gen_obj._libraries['java_sdk']

    if os.path.isfile(os.path.join(jdk.lib_dir, '../bin/java.exe')):
      java_exe = os.path.join(jdk.lib_dir, '../bin/java.exe')

  if not java_exe:
    print('Java not found. Skipping Java tests')
  else:
    args = (os.path.abspath(java_exe),)
    if (objdir == 'Debug'):
      args = args + ('-Xcheck:jni',)

    if cleanup:
      args = args + ('-Dtest.cleanup=1',)

    args = args + (
            '-Dtest.rootdir=' + os.path.join(abs_builddir, 'javahl'),
            '-Dtest.srcdir=' + os.path.join(abs_srcdir,
                                            'subversion/bindings/javahl'),
            '-Dtest.rooturl=',
            '-Dtest.fstype=' + fs_type ,
            '-Dtest.tests=',

            '-Djava.library.path='
                      + os.path.join(abs_objdir,
                                     'subversion/bindings/javahl/native'),
            '-classpath',
            os.path.join(abs_srcdir, 'subversion/bindings/javahl/classes') +';' +
              gen_obj.junit_path
           )

    sys.stderr.flush()
    print('Running org.apache.subversion tests:')
    sys.stdout.flush()

    r = subprocess.call(args + tuple(['org.apache.subversion.javahl.RunTests']))
    sys.stdout.flush()
    sys.stderr.flush()
    if (r != 0):
      print('[Test runner reported failure]')
      failed = True

    print('Running org.tigris.subversion tests:')
    sys.stdout.flush()
    r = subprocess.call(args + tuple(['org.tigris.subversion.javahl.RunTests']))
    sys.stdout.flush()
    sys.stderr.flush()
    if (r != 0):
      print('[Test runner reported failure]')
      failed = True
elif test_swig == 'perl':
  failed = False
  swig_dir = os.path.join(abs_builddir, 'swig')
  swig_pl_dir = os.path.join(swig_dir, 'p5lib')
  swig_pl_svn = os.path.join(swig_pl_dir, 'SVN')
  swig_pl_auto_svn = os.path.join(swig_pl_dir, 'auto', 'SVN')

  create_target_dir(swig_pl_svn)

  for i in gen_obj.graph.get_all_sources(gen_base.DT_INSTALL):
    if isinstance(i, gen_base.TargetSWIG) and i.lang == 'perl':
      mod_dir = os.path.join(swig_pl_auto_svn, '_' + i.name[5:].capitalize())
      create_target_dir(mod_dir)
      copy_changed_file(os.path.join(abs_objdir, i.filename), to_dir=mod_dir)

    elif isinstance(i, gen_base.TargetSWIGLib) and i.lang == 'perl':
      copy_changed_file(os.path.join(abs_objdir, i.filename),
                        to_dir=abs_builddir)

  pm_src = os.path.join(abs_srcdir, 'subversion', 'bindings', 'swig', 'perl',
                        'native')

  tests = []

  for root, dirs, files in os.walk(pm_src):
    for name in files:
      if name.endswith('.pm'):
        fn = os.path.join(root, name)
        copy_changed_file(fn, to_dir=swig_pl_svn)
      elif name.endswith('.t'):
        tests.append(os.path.relpath(os.path.join(root, name), pm_src))

  perl5lib = swig_pl_dir
  if 'PERL5LIB' in os.environ:
    perl5lib += os.pathsep + os.environ['PERL5LIB']

  perl_exe = 'perl.exe'

  print('-- Running Swig Perl tests --')
  sys.stdout.flush()
  old_cwd = os.getcwd()
  try:
    os.chdir(pm_src)

    os.environ['PERL5LIB'] = perl5lib
    os.environ["SVN_DBG_NO_ABORT_ON_ERROR_LEAK"] = 'YES'

    r = subprocess.call([
              perl_exe,
              '-MExtUtils::Command::MM',
              '-e', 'test_harness()'
              ] + tests)
  finally:
    os.chdir(old_cwd)

  if (r != 0):
    print('[Test runner reported failure]')
    failed = True
elif test_swig == 'python':
  failed = False
  swig_dir = os.path.join(abs_builddir, 'swig')
  swig_py_dir = os.path.join(swig_dir, 'pylib')
  swig_py_libsvn = os.path.join(swig_py_dir, 'libsvn')
  swig_py_svn = os.path.join(swig_py_dir, 'svn')

  create_target_dir(swig_py_libsvn)
  create_target_dir(swig_py_svn)

  for i in gen_obj.graph.get_all_sources(gen_base.DT_INSTALL):
    if (isinstance(i, gen_base.TargetSWIG)
        or isinstance(i, gen_base.TargetSWIGLib)) and i.lang == 'python':

      src = os.path.join(abs_objdir, i.filename)
      copy_changed_file(src, to_dir=swig_py_libsvn)

  py_src = os.path.join(abs_srcdir, 'subversion', 'bindings', 'swig', 'python')

  for py_file in os.listdir(py_src):
    if py_file.endswith('.py'):
      copy_changed_file(os.path.join(py_src, py_file),
                        to_dir=swig_py_libsvn)

  py_src_svn = os.path.join(py_src, 'svn')
  for py_file in os.listdir(py_src_svn):
    if py_file.endswith('.py'):
      copy_changed_file(os.path.join(py_src_svn, py_file),
                        to_dir=swig_py_svn)

  print('-- Running Swig Python tests --')
  sys.stdout.flush()

  pythonpath = swig_py_dir
  if 'PYTHONPATH' in os.environ:
    pythonpath += os.pathsep + os.environ['PYTHONPATH']

  python_exe = 'python.exe'
  old_cwd = os.getcwd()
  try:
    os.environ['PYTHONPATH'] = pythonpath

    r = subprocess.call([
              python_exe,
              os.path.join(py_src, 'tests', 'run_all.py')
              ])
  finally:
    os.chdir(old_cwd)

    if (r != 0):
      print('[Test runner reported failure]')
      failed = True

elif test_swig == 'ruby':
  failed = False

  if 'ruby' not in gen_obj._libraries:
    print('Ruby not found. Skipping Ruby tests')
  else:
    ruby_lib = gen_obj._libraries['ruby']

    ruby_exe = 'ruby.exe'
    ruby_subdir = os.path.join('subversion', 'bindings', 'swig', 'ruby')
    ruby_args = [
        '-I', os.path.join(abs_srcdir, ruby_subdir),
        os.path.join(abs_srcdir, ruby_subdir, 'test', 'run-test.rb'),
        '--verbose'
      ]

    print('-- Running Swig Ruby tests --')
    sys.stdout.flush()
    old_cwd = os.getcwd()
    try:
      os.chdir(ruby_subdir)

      os.environ["BUILD_TYPE"] = objdir
      os.environ["SVN_DBG_NO_ABORT_ON_ERROR_LEAK"] = 'YES'
      r = subprocess.call([ruby_exe] + ruby_args)
    finally:
      os.chdir(old_cwd)

    sys.stdout.flush()
    sys.stderr.flush()
    if (r != 0):
      print('[Test runner reported failure]')
      failed = True

elif test_swig:
  print('Unknown Swig binding type: ' + str(test_swig))
  failed = True

# Stop service daemon, if any
if daemon:
  del daemon

if memcached:
  del memcached

# Remove the execs again
for tgt in copied_execs:
  try:
    if os.path.isfile(tgt):
      if verbose:
        print("kill: %s" % tgt)
      os.unlink(tgt)
  except:
    traceback.print_exc(file=sys.stdout)
    pass


if failed:
  sys.exit(1)
