# ResCtrl Filesystem Function Flow Diagrams

This document contains mermaid diagrams showing the important function flows in the ResCtrl filesystem implementation (`fs/resctrl/rdtgroup.c`).

## 1. Initialization Flow - Directory Structure Creation

This diagram shows how the ResCtrl filesystem initializes and creates its directory structure during mount.

```mermaid
graph TD
    A[resctrl_init] --> B[register_filesystem]
    A --> C[rdtgroup_setup_default]
    A --> D[sysfs_create_mount_point]
    A --> E[debugfs_create_dir]
    
    F[rdt_get_tree] --> G[rdtgroup_setup_root]
    F --> H[rdtgroup_create_info_dir]
    F --> I[mkdir_mondata_all]
    F --> J[closid_init]
    F --> K[Add base files to root]
    
    G --> L[kernfs_create_root]
    
    H --> M[Create /info directory]
    H --> N[Create resource subdirs]
    N --> O[L3, L2, MB directories]
    
    I --> P[Create /mon_data directory]
    I --> Q[mkdir_mondata_subdir_alldom]
    Q --> R[mkdir_mondata_subdir]
    R --> S[Domain-specific dirs]
    S --> T[mon_L3_00, mon_L3_01, etc.]
    
    style A fill:#e1f5fe
    style F fill:#e8f5e8
    style H fill:#fff3e0
    style I fill:#f3e5f5
```

## 2. Resource Group Creation Flow (mkdir operation)

This diagram shows the flow when creating new resource groups via `mkdir`.

```mermaid
graph TD
    A[rdtgroup_mkdir] --> B{Group Type?}
    
    B -->|Control+Monitor| C[rdtgroup_mkdir_ctrl_mon]
    B -->|Monitor Only| D[rdtgroup_mkdir_mon]
    
    C --> E[mkdir_rdt_prepare]
    C --> F[mkdir_rdt_prepare_rmid_alloc]
    C --> G[mongroup_create_dir]
    
    D --> H[mkdir_rdt_prepare]
    D --> I[mkdir_rdt_prepare_rmid_alloc]
    
    E --> J[Allocate rdtgroup struct]
    E --> K[Setup kernfs node]
    E --> L[Allocate CLOSID if needed]
    
    F --> M[Allocate RMID]
    F --> N[mkdir_mondata_all]
    
    N --> O[Create mon_data structure]
    N --> P[mkdir_mondata_subdir_alldom]
    P --> Q[Create domain directories]
    Q --> R[Add monitoring files]
    
    G --> S[Create mon_groups subdir]
    
    style A fill:#e1f5fe
    style C fill:#e8f5e8
    style D fill:#fff3e0
    style N fill:#f3e5f5
```

## 3. Resource Group Deletion Flow (rmdir operation)

This diagram shows the flow when removing resource groups via `rmdir`.

```mermaid
graph TD
    A[rdtgroup_rmdir] --> B{Group Type?}
    
    B -->|Control Group| C[rdtgroup_rmdir_ctrl]
    B -->|Monitor Group| D[rdtgroup_rmdir_mon]
    
    C --> E[rdt_move_group_tasks]
    C --> F[update_closid_rmid]
    C --> G[rdtgroup_ctrl_remove]
    
    D --> H[rdt_move_group_tasks]
    D --> I[update_closid_rmid]
    D --> J[free_rmid]
    
    E --> K[Move tasks to parent group]
    H --> L[Move tasks to parent group]
    
    F --> M[Update MSRs on all CPUs]
    I --> N[Update MSRs on all CPUs]
    
    G --> O[closid_free]
    G --> P[Remove from rdt_all_groups]
    G --> Q[kernfs_remove]
    
    J --> R[Release monitoring ID]
    
    style A fill:#e1f5fe
    style C fill:#ffebee
    style D fill:#fff3e0
    style E fill:#f3e5f5
    style H fill:#f3e5f5
```

## 4. Task Assignment Flow (tasks file operations)

This diagram shows how tasks are assigned to resource groups via the tasks file.

```mermaid
graph TD
    A[rdtgroup_tasks_write] --> B[Parse PID list]
    B --> C[For each PID]
    
    C --> D[rdtgroup_move_task]
    D --> E[find_task_by_vpid]
    D --> F[__rdtgroup_move_task]
    
    F --> G[task_in_rdtgroup]
    G --> H{Already in group?}
    H -->|No| I[rdt_move_group_tasks]
    H -->|Yes| J[Skip]
    
    I --> K[Update task closid/rmid]
    I --> L[Move task to new group]
    
    K --> M[Context switch or IPI]
    M --> N[Update MSRs]
    
    O[rdtgroup_tasks_show] --> P[rdtgroup_kn_lock_live]
    P --> Q[show_rdt_tasks]
    Q --> R[for_each_process_thread]
    R --> S[is_closid_match]
    R --> T[is_rmid_match]
    S --> U[Output matching tasks]
    T --> U
    
    style A fill:#e1f5fe
    style O fill:#e8f5e8
    style D fill:#fff3e0
    style I fill:#f3e5f5
    style Q fill:#f1f8e9
```

## 5. Monitoring Data Flow (reading monitoring counters)

This diagram shows how monitoring data is read from the mon_data directories.

```mermaid
graph TD
    A[rdtgroup_mondata_show] --> B[Parse kernfs node]
    B --> C[Extract resource/domain/event]
    C --> D[Locate monitoring domain]
    D --> E[Read counter values]
    E --> F[Format output]
    
    G[mkdir_mondata_all] --> H[mongroup_create_dir]
    G --> I[mkdir_mondata_subdir_alldom]
    
    I --> J[mkdir_mondata_subdir]
    J --> K[Create domain directory]
    K --> L[Create event files]
    L --> M[Link to mon_data_kn_priv_list]
    
    N[mon_get_kn_priv] --> O[Get private data]
    P[mon_put_kn_priv] --> Q[Cleanup private data]
    
    style A fill:#e1f5fe
    style G fill:#e8f5e8
    style J fill:#fff3e0
    style L fill:#f3e5f5
```

## 6. Pseudolock Flow (cache pseudo-locking)

This diagram shows the flow for setting up cache pseudo-locking via the mode file.

```mermaid
graph TD
    A[rdtgroup_mode_write] --> B[Parse mode string]
    B --> C{Mode?}
    
    C -->|pseudo-locksetup| D[rdtgroup_pseudo_lock_create]
    C -->|shareable/exclusive| E[rdtgroup_pseudo_lock_remove]
    
    D --> F[Validate pseudolock requirements]
    D --> G[Setup pseudolock state]
    G --> H[rdt_pseudo_lock_init]
    
    E --> I[Cleanup pseudolock state]
    E --> J[rdt_pseudo_lock_release]
    
    K[rdtgroup_mode_show] --> L[Return current mode]
    L --> M[shareable/exclusive/pseudo-locksetup/pseudo-locked]
    
    style A fill:#e1f5fe
    style K fill:#e8f5e8
    style D fill:#fff3e0
    style E fill:#ffebee
    style H fill:#f3e5f5
```

## 7. Overall Filesystem Operations Structure

This diagram shows the high-level structure of ResCtrl filesystem operations.

```mermaid
graph TD
    A[kernfs_syscall_ops] --> B[rdtgroup_mkdir]
    A --> C[rdtgroup_rmdir]
    A --> D[rdtgroup_rename]
    A --> E[rdtgroup_show_options]
    
    F[File Operations] --> G[rdtgroup_kf_single_ops]
    F --> H[kf_mondata_ops]
    F --> I[rdtgroup_kf_multi_ops]
    
    G --> J[tasks, schemata files]
    H --> K[monitoring data files]
    I --> L[multi-value files]
    
    M[Locking] --> N[rdtgroup_kn_lock_live]
    M --> O[rdtgroup_mutex]
    M --> P[rdt_last_cmd_*]
    
    N --> Q[Kernfs node locking]
    O --> R[Global resctrl mutex]
    P --> S[Error reporting]
    
    style A fill:#e1f5fe
    style F fill:#e8f5e8
    style M fill:#fff3e0
```

## Key Data Structures

The following key data structures are used throughout these flows:

- **`struct rdtgroup`**: Represents a resource group
- **`struct rdt_resource`**: Represents a hardware resource (L3, L2, MB)
- **`struct rdt_domain`**: Represents a domain within a resource
- **`struct kernfs_node`**: Kernel filesystem node
- **`struct rdt_fs_context`**: Filesystem context for mounting

These diagrams show the main function flows within the ResCtrl filesystem, focusing on the core operations of initialization, resource group management, task assignment, monitoring, and special features like pseudo-locking.