#!/usr/bin/env python
#
# check.py :  Run all the test cases.
#
# ===================================================================
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
# ===================================================================
#

import sys
import glob
import subprocess
import os


if __name__ == '__main__':
  # get the test directory from the commandline, if set.
  if len(sys.argv) > 1:
    testdir = sys.argv[1]
  else:
    testdir = 'test'

  if len(sys.argv) > 2:
    test_builddir = sys.argv[2]
  else:
    test_builddir = 'test'

  # define test executable paths
  if sys.platform == 'win32':
    SERF_RESPONSE_EXE = 'serf_response.exe'
    TEST_ALL_EXE = 'test_all.exe'
  else:
    SERF_RESPONSE_EXE = 'serf_response'
    TEST_ALL_EXE = 'test_all'
  SERF_RESPONSE_EXE = os.path.join(test_builddir, SERF_RESPONSE_EXE)
  TEST_ALL_EXE = os.path.join(test_builddir, TEST_ALL_EXE)

  # Find test responses and run them one by one
  for case in glob.glob(testdir + "/testcases/*.response"):
    print "== Testing %s ==" % (case)
    try:
      subprocess.check_call([SERF_RESPONSE_EXE, case])
    except subprocess.CalledProcessError:
      print "ERROR: test case %s failed" % (case)
      sys.exit(1)

  print "== Running the unit tests =="
  try:
    subprocess.check_call(TEST_ALL_EXE)
  except subprocess.CalledProcessError:
    print "ERROR: test(s) failed in test_all"
    sys.exit(1)
