pipeline {
    agent any

    stages {
        stage('Clone') {
            steps {
                echo 'Cloning repository...'
                checkout scm
            }
        }

        stage('Build') {
            steps {
                echo 'Building project...'
                sh 'chmod +x *.sh'   // If you have shell scripts
                sh './your-script.sh'  // 👉 మీ actual build script
            }
        }

        stage('Test') {
            steps {
                echo 'Running tests...'
                // Add test commands here if any
            }
        }

        stage('Deploy') {
            steps {
                echo 'Deploying...'
                // Add deployment steps if any
            }
        }
    }
}
