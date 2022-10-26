# Copyright (C) 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

### build/kernel/kleaf/bazel.WORKSPACE contents ###
load("//build/kernel/kleaf:workspace.bzl", "define_kleaf_workspace")

define_kleaf_workspace()

# Optional epilog for analysis testing.
load("//build/kernel/kleaf:workspace_epilog.bzl", "define_kleaf_workspace_epilog")
define_kleaf_workspace_epilog()

### Qualcomm customizations ###
new_local_repository(
    name = "dtc",
    path = "external/dtc",
    build_file = "msm-kernel/BUILD.dtc",
)
